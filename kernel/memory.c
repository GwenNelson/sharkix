#include <stdint.h>
#include "memory.h"

extern uint64_t bootstrap_pml4[];
extern void vPortInstallKernelGDT(void);

#define ADDRESS_SPACE_LIMIT 8

address_space_t kernel_address_space;
static address_space_t address_spaces[ADDRESS_SPACE_LIMIT];
static unsigned address_space_count;
static uint64_t next_physical_page = 0x00400000ULL;
static uint64_t heap_break_value = KHEAP_BASE;

void *phys_to_virt(uint64_t physical_address)
{
    return (void *)(uintptr_t)(PHYSMAP_BASE + physical_address);
}

uint64_t virt_to_phys(const void *virtual_address)
{
    uint64_t address = (uint64_t)(uintptr_t)virtual_address;
    if (address < PHYSMAP_BASE) return UINT64_MAX;
    return address - PHYSMAP_BASE;
}

uint64_t kernel_heap_break(void) { return heap_break_value; }

uint64_t phys_alloc_page(void)
{
    uint64_t page = next_physical_page;
    uint64_t *words = (uint64_t *)phys_to_virt(page);
    for (unsigned i = 0; i < PAGE_SIZE / sizeof(*words); ++i) words[i] = 0;
    next_physical_page += PAGE_SIZE;
    return page;
}

static uint64_t *page_table(uint64_t physical)
{
    return (uint64_t *)phys_to_virt(physical & ~(PAGE_SIZE - 1));
}

address_space_t *address_space_create(void)
{
    if (address_space_count == ADDRESS_SPACE_LIMIT) return 0;
    address_space_t *address_space = &address_spaces[address_space_count++];
    address_space->pml4_phys = phys_alloc_page();
    uint64_t *destination = page_table(address_space->pml4_phys);
    uint64_t *kernel_pml4 = page_table(kernel_address_space.pml4_phys);
    for (unsigned i = 256; i != 512; ++i) destination[i] = kernel_pml4[i];
    return address_space;
}

void address_space_destroy(address_space_t *address_space)
{
    (void)address_space; /* Monotonic bootstrap allocator for this milestone. */
}

int address_space_map_page(address_space_t *address_space, uintptr_t va,
                           uintptr_t pa, unsigned flags)
{
    if (!address_space || va >= 0x0000800000000000ULL || (va & (PAGE_SIZE - 1)) ||
        (pa & (PAGE_SIZE - 1))) return -1;
    unsigned pml4_index = (unsigned)((va >> 39) & 0x1ff);
    unsigned pdpt_index = (unsigned)((va >> 30) & 0x1ff);
    unsigned pd_index = (unsigned)((va >> 21) & 0x1ff);
    unsigned pt_index = (unsigned)((va >> 12) & 0x1ff);
    uint64_t *pml4 = page_table(address_space->pml4_phys);
    if (!(pml4[pml4_index] & PAGE_PRESENT)) pml4[pml4_index] = phys_alloc_page() | PAGE_PRESENT | PAGE_WRITABLE;
    uint64_t *pdpt = page_table(pml4[pml4_index]);
    if (!(pdpt[pdpt_index] & PAGE_PRESENT)) pdpt[pdpt_index] = phys_alloc_page() | PAGE_PRESENT | PAGE_WRITABLE;
    uint64_t *pd = page_table(pdpt[pdpt_index]);
    if (!(pd[pd_index] & PAGE_PRESENT)) pd[pd_index] = phys_alloc_page() | PAGE_PRESENT | PAGE_WRITABLE;
    uint64_t *pt = page_table(pd[pd_index]);
    pt[pt_index] = (pa & ~(PAGE_SIZE - 1)) | (flags | PAGE_PRESENT);
    return 0;
}

uint64_t address_space_translate(address_space_t *address_space, uintptr_t va)
{
    uint64_t *pml4 = page_table(address_space->pml4_phys);
    uint64_t entry = pml4[(va >> 39) & 0x1ff];
    if (!(entry & PAGE_PRESENT)) return UINT64_MAX;
    uint64_t *pdpt = page_table(entry);
    entry = pdpt[(va >> 30) & 0x1ff];
    if (!(entry & PAGE_PRESENT)) return UINT64_MAX;
    uint64_t *pd = page_table(entry);
    entry = pd[(va >> 21) & 0x1ff];
    if (!(entry & PAGE_PRESENT) || (entry & 0x80)) return UINT64_MAX;
    uint64_t *pt = page_table(entry);
    entry = pt[(va >> 12) & 0x1ff];
    if (!(entry & PAGE_PRESENT)) return UINT64_MAX;
    return (entry & ~(PAGE_SIZE - 1)) | (va & (PAGE_SIZE - 1));
}

void address_space_activate(address_space_t *address_space)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(address_space->pml4_phys) : "memory");
}

void memory_init(void)
{
    vPortInstallKernelGDT();
    kernel_address_space.pml4_phys = (uint64_t)(uintptr_t)bootstrap_pml4;
    page_table(kernel_address_space.pml4_phys)[0] = 0;
    address_space_activate(&kernel_address_space);
}
