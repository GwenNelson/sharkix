#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "memory.h"

extern uint64_t bootstrap_pml4[];
extern void vPortInstallKernelGDT(void);

static address_space_t kernel_address_space;
static uint64_t next_physical_page = 0x00400000ULL;
static uint64_t free_physical_pages;
static uint64_t allocated_pages;
static uint64_t heap_break_value = KHEAP_BASE;

void *phys_to_virt(uint64_t physical_address) { return (void *)(uintptr_t)(PHYSMAP_BASE + physical_address); }
uint64_t virt_to_phys(const void *virtual_address)
{
    uint64_t address = (uint64_t)(uintptr_t)virtual_address;
    return address < PHYSMAP_BASE ? UINT64_MAX : address - PHYSMAP_BASE;
}
uint64_t kernel_heap_break(void) { return heap_break_value; }
uint64_t phys_pages_in_use(void) { return allocated_pages; }

uint64_t phys_alloc_page(void)
{
    uint64_t page;
    if (free_physical_pages) {
        page = free_physical_pages;
        free_physical_pages = *(uint64_t *)phys_to_virt(page);
    } else {
        page = next_physical_page;
        next_physical_page += PAGE_SIZE;
    }
    uint64_t *words = (uint64_t *)phys_to_virt(page);
    for (unsigned i = 0; i < PAGE_SIZE / sizeof(*words); ++i) words[i] = 0;
    ++allocated_pages;
    return page;
}

void phys_free_page(uint64_t page)
{
    if (!page || (page & (PAGE_SIZE - 1))) return;
    *(uint64_t *)phys_to_virt(page) = free_physical_pages;
    free_physical_pages = page;
    if (allocated_pages) --allocated_pages;
}

static uint64_t *page_table(uint64_t physical) { return (uint64_t *)phys_to_virt(physical & PAGE_ADDR_MASK); }

static void free_lower_tables(uint64_t table_physical, unsigned level)
{
    uint64_t *table = page_table(table_physical);
    for (unsigned i = 0; i < 512; ++i)
        if ((table[i] & PAGE_PRESENT) && level > 1) free_lower_tables(table[i] & PAGE_ADDR_MASK, level - 1);
    phys_free_page(table_physical);
}

address_space_t *address_space_kernel(void) { return &kernel_address_space; }

uint32_t address_space_references(const address_space_t *address_space)
{
    return address_space ? address_space->references : 0;
}

void address_space_retain(address_space_t *address_space)
{
    if (address_space && !address_space->permanent) ++address_space->references;
}

static void address_space_destroy(address_space_t *address_space)
{
    if (!address_space || address_space->permanent) return;
    address_space_mapping_t *mapping = address_space->mappings;
    while (mapping) {
        address_space_mapping_t *next = mapping->next;
        if (mapping->owned) phys_free_page(mapping->physical);
        vPortFree(mapping);
        mapping = next;
    }
    uint64_t *pml4 = page_table(address_space->pml4_phys);
    for (unsigned i = 0; i < 256; ++i)
        if (pml4[i] & PAGE_PRESENT) free_lower_tables(pml4[i] & PAGE_ADDR_MASK, 3);
    phys_free_page(address_space->pml4_phys);
    vPortFree(address_space);
}

void address_space_release(address_space_t *address_space)
{
    if (!address_space || address_space->permanent) return;
    if (!address_space->references) return;
    if (--address_space->references == 0) address_space_destroy(address_space);
}

address_space_t *address_space_create(uint32_t flags)
{
    address_space_t *address_space = pvPortMalloc(sizeof(*address_space));
    if (!address_space) return NULL;
    address_space->pml4_phys = phys_alloc_page();
    address_space->mappings = NULL;
    address_space->flags = flags;
    address_space->references = 1;
    address_space->live_threads = 0;
    address_space->permanent = 0;
    uint64_t *destination = page_table(address_space->pml4_phys);
    uint64_t *kernel_pml4 = page_table(address_space_kernel()->pml4_phys);
    for (unsigned i = 256; i < 512; ++i) destination[i] = kernel_pml4[i];
    return address_space;
}

int address_space_map_page(address_space_t *address_space, uintptr_t va, uint64_t pa, uint64_t flags)
{
    if (!address_space || va >= USER_CANONICAL_TOP || (va & (PAGE_SIZE - 1)) || (pa & (PAGE_SIZE - 1))) return -1;
    address_space_mapping_t *mapping = pvPortMalloc(sizeof(*mapping));
    if (!mapping) return -1;
    uint64_t *pml4 = page_table(address_space->pml4_phys);
    unsigned pml4_index = (unsigned)((va >> 39) & 0x1ff), pdpt_index = (unsigned)((va >> 30) & 0x1ff);
    unsigned pd_index = (unsigned)((va >> 21) & 0x1ff), pt_index = (unsigned)((va >> 12) & 0x1ff);
    if (!(pml4[pml4_index] & PAGE_PRESENT)) pml4[pml4_index] = phys_alloc_page() | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    uint64_t *pdpt = page_table(pml4[pml4_index]);
    if (!(pdpt[pdpt_index] & PAGE_PRESENT)) pdpt[pdpt_index] = phys_alloc_page() | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    uint64_t *pd = page_table(pdpt[pdpt_index]);
    if (!(pd[pd_index] & PAGE_PRESENT)) pd[pd_index] = phys_alloc_page() | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    uint64_t *pt = page_table(pd[pd_index]);
    if (pt[pt_index] & PAGE_PRESENT) { vPortFree(mapping); return -1; }
    pt[pt_index] = (pa & PAGE_ADDR_MASK) | PAGE_PRESENT | (flags & (PAGE_WRITABLE | PAGE_USER | PAGE_NX));
    mapping->next = address_space->mappings;
    mapping->virtual_address = va;
    mapping->physical = pa;
    mapping->owned = (flags & ADDRESS_SPACE_MAP_OWNED) != 0;
    address_space->mappings = mapping;
    return 0;
}

int address_space_unmap_page(address_space_t *address_space, uintptr_t va)
{
    if (!address_space || va >= USER_CANONICAL_TOP || (va & (PAGE_SIZE - 1))) return -1;
    uint64_t *pml4 = page_table(address_space->pml4_phys);
    uint64_t entry = pml4[(va >> 39) & 0x1ff]; if (!(entry & PAGE_PRESENT)) return -1;
    uint64_t *pdpt = page_table(entry); entry = pdpt[(va >> 30) & 0x1ff]; if (!(entry & PAGE_PRESENT)) return -1;
    uint64_t *pd = page_table(entry); entry = pd[(va >> 21) & 0x1ff]; if (!(entry & PAGE_PRESENT)) return -1;
    uint64_t *pt = page_table(entry); unsigned index = (unsigned)((va >> 12) & 0x1ff);
    if (!(pt[index] & PAGE_PRESENT)) return -1;
    pt[index] = 0;
    address_space_mapping_t **link = &address_space->mappings;
    while (*link && (*link)->virtual_address != va) link = &(*link)->next;
    if (*link) {
        address_space_mapping_t *mapping = *link;
        *link = mapping->next;
        if (mapping->owned) phys_free_page(mapping->physical);
        vPortFree(mapping);
    }
    return 0;
}

uint64_t address_space_translate(address_space_t *address_space, uintptr_t va)
{
    if (!address_space) return UINT64_MAX;
    uint64_t *pml4 = page_table(address_space->pml4_phys);
    uint64_t entry = pml4[(va >> 39) & 0x1ff]; if (!(entry & PAGE_PRESENT)) return UINT64_MAX;
    uint64_t *pdpt = page_table(entry); entry = pdpt[(va >> 30) & 0x1ff]; if (!(entry & PAGE_PRESENT)) return UINT64_MAX;
    uint64_t *pd = page_table(entry); entry = pd[(va >> 21) & 0x1ff]; if (!(entry & PAGE_PRESENT) || (entry & 0x80)) return UINT64_MAX;
    uint64_t *pt = page_table(entry); entry = pt[(va >> 12) & 0x1ff]; if (!(entry & PAGE_PRESENT)) return UINT64_MAX;
    return (entry & PAGE_ADDR_MASK) | (va & (PAGE_SIZE - 1));
}

void address_space_activate(address_space_t *address_space)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(address_space->pml4_phys) : "memory");
}

void memory_init(void)
{
    vPortInstallKernelGDT();
    kernel_address_space.pml4_phys = (uint64_t)(uintptr_t)bootstrap_pml4;
    kernel_address_space.mappings = NULL;
    kernel_address_space.flags = 0;
    kernel_address_space.references = UINT32_MAX;
    kernel_address_space.live_threads = 0;
    kernel_address_space.permanent = 1;
    page_table(kernel_address_space.pml4_phys)[0] = 0;
    address_space_activate(address_space_kernel());
}
