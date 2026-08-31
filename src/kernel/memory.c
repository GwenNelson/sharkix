#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "memory.h"
#include "sharkix/kernel/boot/multiboot1.h"

#define BOOTSTRAP_PHYS_WINDOW_LIMIT   0x40000000ULL
#define BOOTSTUB32_PHYS_BASE          0x02000000ULL
#define BOOTSTUB32_RESERVED_BYTES     0x00200000ULL
#define LEGACY_DMA_RESERVE_LIMIT      0x01000000ULL
#define PHYSMAP_GIB                   0x40000000ULL
#define HUGE_PAGE_SIZE                0x00200000ULL
#define HUGE_PAGE_FLAG                0x080ULL
#define KHEAP_LIMIT                   0xffffe00000000000ULL
#define KSTACK_BASE                   0xffffe00000000000ULL
#define KSTACK_LIMIT                  0xfffff00000000000ULL
#define KMALLOC_ALIGNMENT             16ULL
#define KMALLOC_MIN_SPLIT             32ULL
#define MAX_BOOT_RANGES               128U

typedef struct boot_range {
    uint64_t start;
    uint64_t end;
} boot_range_t;

typedef struct bootstrap_allocator {
    uint64_t start;
    uint64_t current;
    uint64_t end;
    unsigned active;
} bootstrap_allocator_t;

typedef struct kmalloc_chunk {
    size_t size;
    unsigned free;
    struct kmalloc_chunk *next;
    struct kmalloc_chunk *prev;
} kmalloc_chunk_t;

typedef struct page_walk {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    unsigned pml4_index;
    unsigned pdpt_index;
    unsigned pd_index;
    unsigned pt_index;
} page_walk_t;

extern uint64_t bootstrap_pml4[];
extern uint64_t __kernel_physical_start;
extern uint64_t __kernel_physical_end;
extern void vPortInstallKernelGDT(void);

static address_space_t kernel_address_space;
static bootstrap_allocator_t bootstrap_allocator;
static boot_range_t usable_ranges[MAX_BOOT_RANGES];
static boot_range_t reserved_ranges[MAX_BOOT_RANGES];
static uint32_t usable_range_count;
static uint32_t reserved_range_count;

static uint64_t tracked_phys_limit;
static uint64_t total_ram_bytes_value;
static uint64_t managed_page_count_value;
static uint64_t free_page_count_value;
static uint64_t general_search_hint;
static uint64_t constrained_search_hint;
static uint64_t general_pool_floor_page;
static uint64_t heap_break_value = KHEAP_BASE;
static uint64_t heap_mapped_break = KHEAP_BASE;
static uint64_t next_kernel_stack_base = KSTACK_BASE + PAGE_SIZE;
static uint8_t *alloc_bitmap;
static uint32_t *page_refcounts;
static kmalloc_chunk_t *kmalloc_head;

static void memory_halt(void) __attribute__((noreturn));
static void memory_halt(void)
{
    for (;;) __asm__ volatile ("cli; hlt");
}

static void memory_panic(const char *message) __attribute__((noreturn));
static void memory_panic(const char *message)
{
    console_write("memory panic: ");
    console_write(message);
    console_write("\n");
    memory_halt();
}

static uint64_t align_up_u64(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1ULL) & ~(alignment - 1ULL);
}

static uint64_t align_down_u64(uint64_t value, uint64_t alignment)
{
    return value & ~(alignment - 1ULL);
}

void *phys_to_virt(uint64_t physical_address)
{
    return (void *)(uintptr_t)(PHYSMAP_BASE + physical_address);
}

uint64_t virt_to_phys(const void *virtual_address)
{
    uint64_t address = (uint64_t)(uintptr_t)virtual_address;
    return address < PHYSMAP_BASE ? UINT64_MAX : address - PHYSMAP_BASE;
}

static uint64_t *page_table(uint64_t physical)
{
    return (uint64_t *)phys_to_virt(physical & PAGE_ADDR_MASK);
}

static void bitmap_set(uint64_t index)
{
    alloc_bitmap[index >> 3] |= (uint8_t)(1U << (index & 7U));
}

static void bitmap_clear(uint64_t index)
{
    alloc_bitmap[index >> 3] &= (uint8_t)~(1U << (index & 7U));
}

static bool bitmap_test(uint64_t index)
{
    return (alloc_bitmap[index >> 3] & (uint8_t)(1U << (index & 7U))) != 0;
}

static uint64_t tracked_page_count(void)
{
    return tracked_phys_limit / PAGE_SIZE;
}

static bool page_index_valid(uint64_t index)
{
    return index < tracked_page_count();
}

static bool phys_page_aligned(uint64_t page)
{
    return (page & (PAGE_SIZE - 1ULL)) == 0;
}

static uint64_t phys_to_page_index(uint64_t physical)
{
    return physical / PAGE_SIZE;
}

static bool phys_page_is_managed(uint64_t page)
{
    if (!phys_page_aligned(page)) return false;
    if (page >= tracked_phys_limit) return false;
    return page_refcounts[phys_to_page_index(page)] != UINT32_MAX;
}

static void reserve_range(uint64_t start, uint64_t end)
{
    if (reserved_range_count >= MAX_BOOT_RANGES) memory_panic("too many reserved ranges");
    if (end <= start) return;
    reserved_ranges[reserved_range_count].start = start;
    reserved_ranges[reserved_range_count].end = end;
    ++reserved_range_count;
}

static void add_usable_range(uint64_t start, uint64_t end)
{
    if (usable_range_count >= MAX_BOOT_RANGES) memory_panic("too many usable ranges");
    if (end <= start) return;
    usable_ranges[usable_range_count].start = start;
    usable_ranges[usable_range_count].end = end;
    ++usable_range_count;
}

static bool ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start, uint64_t b_end)
{
    return a_start < b_end && b_start < a_end;
}

static bool range_is_reserved(uint64_t start, uint64_t end)
{
    for (uint32_t i = 0; i < reserved_range_count; ++i)
        if (ranges_overlap(start, end, reserved_ranges[i].start, reserved_ranges[i].end))
            return true;
    return false;
}

static void reserve_pointer_page(uint32_t pointer)
{
    if (!pointer) return;
    reserve_range(align_down_u64(pointer, PAGE_SIZE), align_up_u64((uint64_t)pointer + 1ULL, PAGE_SIZE));
}

static void reserve_multiboot_payloads(const multiboot_info_t *mbi)
{
    reserve_range(align_down_u64((uint64_t)(uintptr_t)mbi, PAGE_SIZE),
                  align_up_u64((uint64_t)(uintptr_t)mbi + sizeof(*mbi), PAGE_SIZE));

    if (mbi->flags & MB_INFO_CMDLINE) reserve_pointer_page(mbi->cmdline);
    if (mbi->flags & MB_INFO_BOOT_LOADER) reserve_pointer_page(mbi->boot_loader_name);
    if (mbi->flags & MB_INFO_CONFIG_TABLE) reserve_pointer_page(mbi->config_table);
    if (mbi->flags & MB_INFO_APM_TABLE) reserve_pointer_page(mbi->apm_table);
    if (mbi->flags & MB_INFO_DRIVES)
        reserve_range(align_down_u64(mbi->drives_addr, PAGE_SIZE),
                      align_up_u64((uint64_t)mbi->drives_addr + mbi->drives_length, PAGE_SIZE));
    if (mbi->flags & MB_INFO_MMAP)
        reserve_range(align_down_u64(mbi->mmap_addr, PAGE_SIZE),
                      align_up_u64((uint64_t)mbi->mmap_addr + mbi->mmap_length, PAGE_SIZE));
    if (mbi->flags & MB_INFO_MODS) {
        reserve_range(align_down_u64(mbi->mods_addr, PAGE_SIZE),
                      align_up_u64((uint64_t)mbi->mods_addr + (uint64_t)mbi->mods_count * sizeof(multiboot_module_t), PAGE_SIZE));
        multiboot_module_t *mods = (multiboot_module_t *)(uintptr_t)mbi->mods_addr;
        for (uint32_t i = 0; i < mbi->mods_count; ++i) {
            reserve_range(align_down_u64(mods[i].start, PAGE_SIZE), align_up_u64(mods[i].end, PAGE_SIZE));
            reserve_pointer_page(mods[i].string);
        }
    }
}

static void parse_multiboot_memory_map(const multiboot_info_t *mbi)
{
    uint64_t highest_usable_end = 0;

    usable_range_count = 0;
    reserved_range_count = 0;
    total_ram_bytes_value = 0;

    if (mbi->flags & MB_INFO_MMAP) {
        uintptr_t cursor = (uintptr_t)mbi->mmap_addr;
        uintptr_t end = cursor + mbi->mmap_length;

        while (cursor + sizeof(multiboot_mmap_entry_t) <= end) {
            multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *)cursor;
            uint64_t entry_end = entry->addr + entry->len;
            if (entry->len && entry_end > entry->addr) {
                if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    add_usable_range(entry->addr, entry_end);
                    total_ram_bytes_value += entry->len;
                    if (entry_end > highest_usable_end) highest_usable_end = entry_end;
                }
            }
            cursor += (uint64_t)entry->size + sizeof(entry->size);
        }
    } else if (mbi->flags & MB_INFO_MEMORY) {
        if (mbi->mem_lower) add_usable_range(0x00000000ULL, (uint64_t)mbi->mem_lower * 1024ULL);
        if (mbi->mem_upper) add_usable_range(0x00100000ULL, 0x00100000ULL + (uint64_t)mbi->mem_upper * 1024ULL);
        total_ram_bytes_value = (uint64_t)(mbi->mem_lower + mbi->mem_upper) * 1024ULL;
        for (uint32_t i = 0; i < usable_range_count; ++i)
            if (usable_ranges[i].end > highest_usable_end) highest_usable_end = usable_ranges[i].end;
    } else {
        memory_panic("bootloader provided no usable memory information");
    }

    if (!usable_range_count || !highest_usable_end) memory_panic("no usable RAM ranges");

    reserve_range(0, 0x00100000ULL);
    reserve_range(0x00100000ULL, align_up_u64((uint64_t)(uintptr_t)&__kernel_physical_end, PAGE_SIZE));
    reserve_range(BOOTSTUB32_PHYS_BASE, BOOTSTUB32_PHYS_BASE + BOOTSTUB32_RESERVED_BYTES);
    reserve_multiboot_payloads(mbi);
    tracked_phys_limit = align_up_u64(highest_usable_end, PAGE_SIZE);
}

static bool find_bootstrap_region(uint64_t bytes, uint64_t *out_start)
{
    uint64_t rounded = align_up_u64(bytes, PAGE_SIZE);

    for (uint32_t i = 0; i < usable_range_count; ++i) {
        uint64_t start = align_up_u64(usable_ranges[i].start, PAGE_SIZE);
        uint64_t end = align_down_u64(usable_ranges[i].end, PAGE_SIZE);
        if (start >= end) continue;
        if (start >= BOOTSTRAP_PHYS_WINDOW_LIMIT) continue;
        if (end > BOOTSTRAP_PHYS_WINDOW_LIMIT) end = BOOTSTRAP_PHYS_WINDOW_LIMIT;
        for (uint64_t candidate = start; candidate + rounded <= end; candidate += PAGE_SIZE) {
            if (!range_is_reserved(candidate, candidate + rounded)) {
                *out_start = candidate;
                return true;
            }
        }
    }
    return false;
}

static void bootstrap_begin(uint64_t start, uint64_t bytes)
{
    bootstrap_allocator.start = start;
    bootstrap_allocator.current = start;
    bootstrap_allocator.end = start + align_up_u64(bytes, PAGE_SIZE);
    bootstrap_allocator.active = 1;
}

static uint64_t bootstrap_alloc(uint64_t bytes, uint64_t alignment)
{
    uint64_t current;

    if (!bootstrap_allocator.active) memory_panic("bootstrap allocator inactive");
    if (!alignment) alignment = PAGE_SIZE;
    current = align_up_u64(bootstrap_allocator.current, alignment);
    if (current > bootstrap_allocator.end || bytes > bootstrap_allocator.end - current)
        memory_panic("bootstrap allocator exhausted");
    bootstrap_allocator.current = current + bytes;
    return current;
}

static uint64_t bootstrap_alloc_page(void)
{
    uint64_t page = bootstrap_alloc(PAGE_SIZE, PAGE_SIZE);
    uint64_t *words = (uint64_t *)phys_to_virt(page);
    for (size_t i = 0; i < PAGE_SIZE / sizeof(*words); ++i) words[i] = 0;
    return page;
}

static void bootstrap_finish(void)
{
    bootstrap_allocator.active = 0;
}

static void expand_physmap(uint64_t limit)
{
    uint64_t rounded_limit = align_up_u64(limit, PHYSMAP_GIB);
    uint64_t needed_gib = rounded_limit / PHYSMAP_GIB;
    uint64_t *pml4 = page_table((uint64_t)(uintptr_t)bootstrap_pml4);
    uint64_t *pdpt = page_table(pml4[256] & PAGE_ADDR_MASK);

    if (needed_gib > 512ULL) memory_panic("physmap limit exceeds one PML4 slot");

    for (uint64_t gib = 1; gib < needed_gib; ++gib) {
        uint64_t pd_phys;
        uint64_t *pd;
        if (pdpt[gib] & PAGE_PRESENT) continue;
        pd_phys = bootstrap_alloc_page();
        pd = page_table(pd_phys);
        for (unsigned i = 0; i < 512; ++i)
            pd[i] = (gib * PHYSMAP_GIB + (uint64_t)i * HUGE_PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE | HUGE_PAGE_FLAG;
        pdpt[gib] = pd_phys | PAGE_PRESENT | PAGE_WRITABLE;
    }

    __asm__ volatile ("mov %%cr3, %%rax; mov %%rax, %%cr3" : : : "rax", "memory");
}

static void allocator_mark_managed_range(uint64_t start, uint64_t end)
{
    uint64_t page_start = align_up_u64(start, PAGE_SIZE);
    uint64_t page_end = align_down_u64(end, PAGE_SIZE);

    for (uint64_t page = page_start; page < page_end; page += PAGE_SIZE) {
        uint64_t index = phys_to_page_index(page);
        if (!page_index_valid(index)) continue;
        if (page_refcounts[index] == UINT32_MAX) {
            bitmap_clear(index);
            page_refcounts[index] = 0;
            ++managed_page_count_value;
            ++free_page_count_value;
        }
    }
}

static void allocator_mark_unmanaged_range(uint64_t start, uint64_t end)
{
    uint64_t page_start = align_down_u64(start, PAGE_SIZE);
    uint64_t page_end = align_up_u64(end, PAGE_SIZE);

    for (uint64_t page = page_start; page < page_end; page += PAGE_SIZE) {
        uint64_t index = phys_to_page_index(page);
        if (!page_index_valid(index)) continue;
        if (page_refcounts[index] != UINT32_MAX) {
            if (!bitmap_test(index) && free_page_count_value) --free_page_count_value;
            if (managed_page_count_value) --managed_page_count_value;
        }
        bitmap_set(index);
        page_refcounts[index] = UINT32_MAX;
    }
}

static void allocator_init_metadata(void)
{
    uint64_t pages = tracked_page_count();
    uint64_t bitmap_bytes = (pages + 7ULL) / 8ULL;
    uint64_t refcount_bytes = pages * sizeof(*page_refcounts);
    uint64_t bitmap_phys = bootstrap_alloc(bitmap_bytes, PAGE_SIZE);
    uint64_t refcount_phys = bootstrap_alloc(refcount_bytes, PAGE_SIZE);

    alloc_bitmap = (uint8_t *)phys_to_virt(bitmap_phys);
    page_refcounts = (uint32_t *)phys_to_virt(refcount_phys);

    for (uint64_t i = 0; i < bitmap_bytes; ++i) alloc_bitmap[i] = 0xff;
    for (uint64_t i = 0; i < pages; ++i) page_refcounts[i] = UINT32_MAX;

    managed_page_count_value = 0;
    free_page_count_value = 0;

    for (uint32_t i = 0; i < usable_range_count; ++i)
        allocator_mark_managed_range(usable_ranges[i].start, usable_ranges[i].end);
    for (uint32_t i = 0; i < reserved_range_count; ++i)
        allocator_mark_unmanaged_range(reserved_ranges[i].start, reserved_ranges[i].end);

    general_pool_floor_page = align_up_u64(LEGACY_DMA_RESERVE_LIMIT, PAGE_SIZE) / PAGE_SIZE;
    general_search_hint = general_pool_floor_page;
    constrained_search_hint = 1;
}

static uint64_t bootstrap_bytes_required(uint64_t phys_limit)
{
    uint64_t pages = align_up_u64(phys_limit, PAGE_SIZE) / PAGE_SIZE;
    uint64_t bitmap_bytes = align_up_u64((pages + 7ULL) / 8ULL, PAGE_SIZE);
    uint64_t refcount_bytes = align_up_u64(pages * sizeof(uint32_t), PAGE_SIZE);
    uint64_t physmap_gib = align_up_u64(phys_limit, PHYSMAP_GIB) / PHYSMAP_GIB;
    uint64_t extra_pd_pages = physmap_gib > 1 ? physmap_gib - 1 : 0;

    return bitmap_bytes + refcount_bytes + extra_pd_pages * PAGE_SIZE;
}

uint64_t kernel_heap_break(void) { return heap_break_value; }
uint64_t phys_total_ram_bytes(void) { return total_ram_bytes_value; }
uint64_t phys_managed_page_count(void) { return managed_page_count_value; }
uint64_t phys_free_page_count(void) { return free_page_count_value; }
uint64_t phys_pages_in_use(void) { return managed_page_count_value - free_page_count_value; }

static uint64_t search_free_run(uint64_t start_page, uint64_t end_page, size_t count)
{
    uint64_t run = 0;
    uint64_t run_start = start_page;

    for (uint64_t page = start_page; page < end_page; ++page) {
        if (!bitmap_test(page) && page_refcounts[page] == 0) {
            if (!run) run_start = page;
            if (++run == count) return run_start;
        } else {
            run = 0;
        }
    }
    return UINT64_MAX;
}

static bool alloc_run_from_interval(uint64_t interval_start, uint64_t interval_end,
                                    uint64_t *hint, size_t count, uint64_t *out_page)
{
    uint64_t start;

    if (count == 0 || interval_start >= interval_end || interval_end - interval_start < count)
        return false;

    start = *hint;
    if (start < interval_start || start >= interval_end) start = interval_start;

    uint64_t found = search_free_run(start, interval_end, count);
    if (found == UINT64_MAX && start > interval_start)
        found = search_free_run(interval_start, start, count);
    if (found == UINT64_MAX) return false;

    for (size_t i = 0; i < count; ++i) {
        uint64_t page = found + i;
        bitmap_set(page);
        page_refcounts[page] = 1;
    }
    free_page_count_value -= count;
    *hint = found + count;
    *out_page = found * PAGE_SIZE;
    return true;
}

bool phys_alloc_pages(size_t count, uint64_t *out_page)
{
    bool allocated;

    vPortEnterCritical();
    allocated = out_page && count != 0 &&
        alloc_run_from_interval(general_pool_floor_page, tracked_page_count(),
                                &general_search_hint, count, out_page);
    vPortExitCritical();
    return allocated;
}

bool phys_alloc_page(uint64_t *out_page)
{
    return phys_alloc_pages(1, out_page);
}

bool phys_alloc_pages_below(size_t count, uint64_t max_phys_addr, uint64_t *out_page)
{
    uint64_t end_page;
    bool allocated;

    if (!out_page || count == 0 || max_phys_addr <= PAGE_SIZE) return false;
    end_page = align_up_u64(max_phys_addr, PAGE_SIZE) / PAGE_SIZE;
    if (end_page > tracked_page_count()) end_page = tracked_page_count();
    vPortEnterCritical();
    allocated = alloc_run_from_interval(1, end_page, &constrained_search_hint, count, out_page);
    vPortExitCritical();
    return allocated;
}

bool phys_alloc_page_below(uint64_t max_phys_addr, uint64_t *out_page)
{
    return phys_alloc_pages_below(1, max_phys_addr, out_page);
}

void phys_page_get(uint64_t page)
{
    uint64_t index;

    vPortEnterCritical();
    if (!phys_page_aligned(page)) memory_panic("phys_page_get unaligned");
    if (!phys_page_is_managed(page)) memory_panic("phys_page_get unmanaged");
    index = phys_to_page_index(page);
    if (page_refcounts[index] == 0) memory_panic("phys_page_get free page");
    if (page_refcounts[index] == UINT32_MAX) memory_panic("phys_page_get reserved page");
    if (page_refcounts[index] == UINT32_MAX - 1U) memory_panic("phys_page_get overflow");
    ++page_refcounts[index];
    vPortExitCritical();
}

void phys_page_put(uint64_t page)
{
    uint64_t index;

    vPortEnterCritical();
    if (!phys_page_aligned(page)) memory_panic("phys_page_put unaligned");
    if (!phys_page_is_managed(page)) memory_panic("phys_page_put unmanaged");
    index = phys_to_page_index(page);
    if (page_refcounts[index] == 0) memory_panic("phys_page_put free page");
    if (--page_refcounts[index] == 0) {
        bitmap_clear(index);
        ++free_page_count_value;
        if (index < general_search_hint && index >= general_pool_floor_page) general_search_hint = index;
        if (index < constrained_search_hint) constrained_search_hint = index;
        uint64_t *words = (uint64_t *)phys_to_virt(page);
        for (size_t i = 0; i < PAGE_SIZE / sizeof(*words); ++i) words[i] = 0;
    }
    vPortExitCritical();
}

static bool page_walk_present(uint64_t entry)
{
    return (entry & PAGE_PRESENT) != 0;
}

static bool walk_to_pte(uint64_t pml4_phys, uintptr_t va, page_walk_t *walk)
{
    walk->pml4 = page_table(pml4_phys);
    walk->pml4_index = (unsigned)((va >> 39) & 0x1ffU);
    if (!page_walk_present(walk->pml4[walk->pml4_index])) return false;
    walk->pdpt = page_table(walk->pml4[walk->pml4_index] & PAGE_ADDR_MASK);
    walk->pdpt_index = (unsigned)((va >> 30) & 0x1ffU);
    if (!page_walk_present(walk->pdpt[walk->pdpt_index])) return false;
    walk->pd = page_table(walk->pdpt[walk->pdpt_index] & PAGE_ADDR_MASK);
    walk->pd_index = (unsigned)((va >> 21) & 0x1ffU);
    if (!page_walk_present(walk->pd[walk->pd_index]) || (walk->pd[walk->pd_index] & HUGE_PAGE_FLAG)) return false;
    walk->pt = page_table(walk->pd[walk->pd_index] & PAGE_ADDR_MASK);
    walk->pt_index = (unsigned)((va >> 12) & 0x1ffU);
    return true;
}

static bool allocate_page_table(uint64_t *out_phys)
{
    if (!phys_alloc_page(out_phys)) return false;
    return true;
}

static bool ensure_page_table(uint64_t pml4_phys, uintptr_t va, bool user, page_walk_t *walk)
{
    uint64_t entry_flags = PAGE_PRESENT | PAGE_WRITABLE | (user ? PAGE_USER : 0);
    uint64_t table_phys;

    walk->pml4 = page_table(pml4_phys);
    walk->pml4_index = (unsigned)((va >> 39) & 0x1ffU);
    if (!page_walk_present(walk->pml4[walk->pml4_index])) {
        if (!allocate_page_table(&table_phys)) return false;
        walk->pml4[walk->pml4_index] = table_phys | entry_flags;
    }

    walk->pdpt = page_table(walk->pml4[walk->pml4_index] & PAGE_ADDR_MASK);
    walk->pdpt_index = (unsigned)((va >> 30) & 0x1ffU);
    if (!page_walk_present(walk->pdpt[walk->pdpt_index])) {
        if (!allocate_page_table(&table_phys)) return false;
        walk->pdpt[walk->pdpt_index] = table_phys | entry_flags;
    }

    walk->pd = page_table(walk->pdpt[walk->pdpt_index] & PAGE_ADDR_MASK);
    walk->pd_index = (unsigned)((va >> 21) & 0x1ffU);
    if (!page_walk_present(walk->pd[walk->pd_index])) {
        if (!allocate_page_table(&table_phys)) return false;
        walk->pd[walk->pd_index] = table_phys | entry_flags;
    } else if (walk->pd[walk->pd_index] & HUGE_PAGE_FLAG) {
        return false;
    }

    walk->pt = page_table(walk->pd[walk->pd_index] & PAGE_ADDR_MASK);
    walk->pt_index = (unsigned)((va >> 12) & 0x1ffU);
    return true;
}

static bool table_empty(uint64_t *table)
{
    for (unsigned i = 0; i < 512; ++i)
        if (table[i] & PAGE_PRESENT) return false;
    return true;
}

static void invalidate_page(uintptr_t va)
{
    __asm__ volatile ("invlpg (%0)" : : "r"((void *)va) : "memory");
}

static bool mapping_allowed(address_space_t *address_space, uintptr_t va, bool kernel_region)
{
    if (!address_space || (va & (PAGE_SIZE - 1ULL))) return false;
    if (kernel_region)
        return va >= KHEAP_BASE && va < KSTACK_LIMIT;
    return va < USER_CANONICAL_TOP;
}

static int map_page_internal(address_space_t *address_space, uintptr_t va, uint64_t pa,
                             uint64_t flags, bool kernel_region, bool track_mapping)
{
    address_space_mapping_t *mapping = NULL;
    page_walk_t walk;
    bool transfer_ref = (flags & ADDRESS_SPACE_MAP_OWNED) != 0;
    bool user = !kernel_region;

    if (!mapping_allowed(address_space, va, kernel_region) || !phys_page_aligned(pa)) return -1;
    if (track_mapping) {
        mapping = kmalloc(sizeof(*mapping));
        if (!mapping) return -1;
    }
    if (!ensure_page_table(address_space->pml4_phys, va, user, &walk)) {
        if (mapping) kfree(mapping);
        return -1;
    }
    if (walk.pt[walk.pt_index] & PAGE_PRESENT) {
        if (mapping) kfree(mapping);
        return -1;
    }

    if (phys_page_is_managed(pa) && !transfer_ref) phys_page_get(pa);
    walk.pt[walk.pt_index] = (pa & PAGE_ADDR_MASK) | PAGE_PRESENT | (flags & (PAGE_WRITABLE | PAGE_USER | PAGE_NX));
    invalidate_page(va);

    if (mapping) {
        mapping->next = address_space->mappings;
        mapping->virtual_address = va;
        mapping->physical = pa;
        mapping->owned = transfer_ref ? 1U : 0U;
        address_space->mappings = mapping;
    }
    return 0;
}

static int unmap_page_internal(address_space_t *address_space, uintptr_t va, bool kernel_region, bool track_mapping)
{
    page_walk_t walk;
    uint64_t table_phys;
    uint64_t pa;

    if (!mapping_allowed(address_space, va, kernel_region)) return -1;
    if (!walk_to_pte(address_space->pml4_phys, va, &walk)) return -1;
    if (!(walk.pt[walk.pt_index] & PAGE_PRESENT)) return -1;

    pa = walk.pt[walk.pt_index] & PAGE_ADDR_MASK;
    walk.pt[walk.pt_index] = 0;
    invalidate_page(va);

    if (phys_page_is_managed(pa)) phys_page_put(pa);

    if (track_mapping) {
        address_space_mapping_t **link = &address_space->mappings;
        while (*link && (*link)->virtual_address != va) link = &(*link)->next;
        if (*link) {
            address_space_mapping_t *mapping = *link;
            *link = mapping->next;
            kfree(mapping);
        }
    }

    if (table_empty(walk.pt)) {
        table_phys = walk.pd[walk.pd_index] & PAGE_ADDR_MASK;
        walk.pd[walk.pd_index] = 0;
        invalidate_page(va);
        if (phys_page_is_managed(table_phys)) phys_page_put(table_phys);
        if (table_empty(walk.pd)) {
            table_phys = walk.pdpt[walk.pdpt_index] & PAGE_ADDR_MASK;
            walk.pdpt[walk.pdpt_index] = 0;
            if (phys_page_is_managed(table_phys)) phys_page_put(table_phys);
            if (table_empty(walk.pdpt) && walk.pml4_index < 256U) {
                table_phys = walk.pml4[walk.pml4_index] & PAGE_ADDR_MASK;
                walk.pml4[walk.pml4_index] = 0;
                if (phys_page_is_managed(table_phys)) phys_page_put(table_phys);
            }
        }
    }
    return 0;
}

address_space_t *address_space_kernel(void) { return &kernel_address_space; }

uint32_t address_space_references(const address_space_t *address_space)
{
    uint32_t references;

    vPortEnterCritical();
    references = address_space ? address_space->references : 0;
    vPortExitCritical();
    return references;
}

void address_space_retain(address_space_t *address_space)
{
    vPortEnterCritical();
    if (address_space && !address_space->permanent) ++address_space->references;
    vPortExitCritical();
}

static void address_space_destroy(address_space_t *address_space)
{
    if (!address_space || address_space->permanent) return;
    while (address_space->mappings)
        if (address_space_unmap_page(address_space, address_space->mappings->virtual_address) != 0)
            memory_panic("address space destroy unmap failed");
    phys_page_put(address_space->pml4_phys);
    kfree(address_space);
}

void address_space_release(address_space_t *address_space)
{
    vPortEnterCritical();
    if (!address_space || address_space->permanent) {
        vPortExitCritical();
        return;
    }
    if (!address_space->references) {
        vPortExitCritical();
        return;
    }
    if (--address_space->references == 0) address_space_destroy(address_space);
    vPortExitCritical();
}

address_space_t *address_space_create(uint32_t flags)
{
    address_space_t *address_space;
    uint64_t pml4_phys;
    uint64_t *destination;
    uint64_t *kernel_pml4;

    vPortEnterCritical();
    if (!phys_alloc_page(&pml4_phys)) {
        vPortExitCritical();
        return NULL;
    }
    address_space = kmalloc(sizeof(*address_space));
    if (!address_space) {
        phys_page_put(pml4_phys);
        vPortExitCritical();
        return NULL;
    }
    destination = page_table(pml4_phys);
    kernel_pml4 = page_table(address_space_kernel()->pml4_phys);
    for (unsigned i = 0; i < 512; ++i) destination[i] = 0;
    for (unsigned i = 256; i < 512; ++i) destination[i] = kernel_pml4[i];

    address_space->pml4_phys = pml4_phys;
    address_space->mappings = NULL;
    address_space->flags = flags;
    address_space->references = 1;
    address_space->live_threads = 0;
    address_space->permanent = 0;
    vPortExitCritical();
    return address_space;
}

int address_space_map_page(address_space_t *address_space, uintptr_t va, uint64_t pa, uint64_t flags)
{
    int result;
    vPortEnterCritical();
    result = map_page_internal(address_space, va, pa, flags, false, true);
    vPortExitCritical();
    return result;
}

int address_space_unmap_page(address_space_t *address_space, uintptr_t va)
{
    int result;
    vPortEnterCritical();
    result = unmap_page_internal(address_space, va, false, true);
    vPortExitCritical();
    return result;
}

uint64_t address_space_translate(address_space_t *address_space, uintptr_t va)
{
    page_walk_t walk;
    uint64_t physical = UINT64_MAX;

    vPortEnterCritical();
    if (address_space && walk_to_pte(address_space->pml4_phys, va, &walk) &&
        (walk.pt[walk.pt_index] & PAGE_PRESENT))
        physical = (walk.pt[walk.pt_index] & PAGE_ADDR_MASK) | (va & (PAGE_SIZE - 1ULL));
    vPortExitCritical();
    return physical;
}

void address_space_activate(address_space_t *address_space)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(address_space->pml4_phys) : "memory");
}

static int kernel_map_page(uintptr_t va, uint64_t pa, uint64_t flags, bool transfer_ref)
{
    return map_page_internal(address_space_kernel(), va, pa,
                             flags | (transfer_ref ? ADDRESS_SPACE_MAP_OWNED : 0), true, false);
}

static int kernel_unmap_page(uintptr_t va)
{
    return unmap_page_internal(address_space_kernel(), va, true, false);
}

void *ksbrk(ptrdiff_t increment)
{
    uint64_t old_break;
    uint64_t new_break;
    void *result;

    vPortEnterCritical();
    old_break = heap_break_value;
    if (increment == 0) {
        result = (void *)(uintptr_t)old_break;
        goto out;
    }
    if (increment > 0) {
        new_break = old_break + (uint64_t)increment;
        if (new_break < old_break || new_break > KHEAP_LIMIT) {
            result = (void *)(intptr_t)-1;
            goto out;
        }

        uint64_t map_start = align_up_u64(heap_mapped_break, PAGE_SIZE);
        uint64_t map_end = align_up_u64(new_break, PAGE_SIZE);
        for (uint64_t va = map_start; va < map_end; va += PAGE_SIZE) {
            uint64_t page;
            if (!phys_alloc_page(&page)) {
                while (va > map_start) {
                    va -= PAGE_SIZE;
                    (void)kernel_unmap_page(va);
                }
                result = (void *)(intptr_t)-1;
                goto out;
            }
            if (kernel_map_page(va, page, PAGE_WRITABLE | PAGE_NX, true) != 0) {
                phys_page_put(page);
                while (va > map_start) {
                    va -= PAGE_SIZE;
                    (void)kernel_unmap_page(va);
                }
                result = (void *)(intptr_t)-1;
                goto out;
            }
        }
        heap_mapped_break = map_end;
        heap_break_value = new_break;
        result = (void *)(uintptr_t)old_break;
        goto out;
    }

    if ((uint64_t)(-increment) > old_break - KHEAP_BASE) {
        result = (void *)(intptr_t)-1;
        goto out;
    }
    new_break = old_break - (uint64_t)(-increment);
    uint64_t unmap_start = align_up_u64(new_break, PAGE_SIZE);
    uint64_t unmap_end = align_up_u64(old_break, PAGE_SIZE);
    for (uint64_t va = unmap_start; va < unmap_end; va += PAGE_SIZE)
        (void)kernel_unmap_page(va);
    heap_break_value = new_break;
    heap_mapped_break = unmap_start;
    result = (void *)(uintptr_t)old_break;
out:
    vPortExitCritical();
    return result;
}

static size_t kmalloc_align(size_t size)
{
    return (size + (size_t)KMALLOC_ALIGNMENT - 1U) & ~((size_t)KMALLOC_ALIGNMENT - 1U);
}

void *kmalloc(size_t size)
{
    kmalloc_chunk_t *chunk;
    size_t wanted;
    void *result = NULL;

    if (!size) return NULL;
    vPortEnterCritical();
    wanted = kmalloc_align(size);

    for (chunk = kmalloc_head; chunk; chunk = chunk->next) {
        if (!chunk->free || chunk->size < wanted) continue;
        if (chunk->size >= wanted + sizeof(*chunk) + KMALLOC_MIN_SPLIT) {
            kmalloc_chunk_t *split = (kmalloc_chunk_t *)((uint8_t *)(chunk + 1) + wanted);
            split->size = chunk->size - wanted - sizeof(*chunk);
            split->free = 1;
            split->next = chunk->next;
            split->prev = chunk;
            if (split->next) split->next->prev = split;
            chunk->next = split;
            chunk->size = wanted;
        }
        chunk->free = 0;
        result = chunk + 1;
        goto out;
    }

    size_t request = align_up_u64(sizeof(*chunk) + wanted, PAGE_SIZE);
    chunk = (kmalloc_chunk_t *)ksbrk((ptrdiff_t)request);
    if (chunk == (void *)(intptr_t)-1) goto out;
    chunk->size = request - sizeof(*chunk);
    chunk->free = 0;
    chunk->next = NULL;
    chunk->prev = NULL;
    if (!kmalloc_head) kmalloc_head = chunk;
    else {
        kmalloc_chunk_t *tail = kmalloc_head;
        while (tail->next) tail = tail->next;
        tail->next = chunk;
        chunk->prev = tail;
    }
    if (chunk->size >= wanted + sizeof(*chunk) + KMALLOC_MIN_SPLIT) {
        kmalloc_chunk_t *split = (kmalloc_chunk_t *)((uint8_t *)(chunk + 1) + wanted);
        split->size = chunk->size - wanted - sizeof(*chunk);
        split->free = 1;
        split->next = chunk->next;
        split->prev = chunk;
        chunk->next = split;
        chunk->size = wanted;
    }
    result = chunk + 1;
out:
    vPortExitCritical();
    return result;
}

void kfree(void *pointer)
{
    kmalloc_chunk_t *chunk;

    if (!pointer) return;
    vPortEnterCritical();
    chunk = (kmalloc_chunk_t *)pointer - 1;
    if (chunk->free) memory_panic("kfree double free");
    chunk->free = 1;

    if (chunk->next && chunk->next->free) {
        chunk->size += sizeof(*chunk) + chunk->next->size;
        chunk->next = chunk->next->next;
        if (chunk->next) chunk->next->prev = chunk;
    }
    if (chunk->prev && chunk->prev->free) {
        chunk->prev->size += sizeof(*chunk) + chunk->size;
        chunk->prev->next = chunk->next;
        if (chunk->next) chunk->next->prev = chunk->prev;
    }
    vPortExitCritical();
}

void *kernel_stack_alloc(size_t size)
{
    size_t rounded = (size + PAGE_SIZE - 1U) & ~(size_t)(PAGE_SIZE - 1U);
    uintptr_t base;

    vPortEnterCritical();
    if (!rounded) rounded = PAGE_SIZE;
    base = (uintptr_t)next_kernel_stack_base;
    if (base + rounded + PAGE_SIZE > KSTACK_LIMIT) {
        vPortExitCritical();
        return NULL;
    }

    for (size_t offset = 0; offset < rounded; offset += PAGE_SIZE) {
        uint64_t page;
        if (!phys_alloc_page(&page)) {
            while (offset) {
                offset -= PAGE_SIZE;
                (void)kernel_unmap_page(base + offset);
            }
            vPortExitCritical();
            return NULL;
        }
        if (kernel_map_page(base + offset, page, PAGE_WRITABLE | PAGE_NX, true) != 0) {
            phys_page_put(page);
            while (offset) {
                offset -= PAGE_SIZE;
                (void)kernel_unmap_page(base + offset);
            }
            vPortExitCritical();
            return NULL;
        }
    }

    next_kernel_stack_base = base + rounded + PAGE_SIZE;
    vPortExitCritical();
    return (void *)base;
}

void kernel_stack_free(void *base_pointer, size_t size)
{
    uintptr_t base = (uintptr_t)base_pointer;
    size_t rounded = (size + PAGE_SIZE - 1U) & ~(size_t)(PAGE_SIZE - 1U);

    if (!base || !rounded) return;
    vPortEnterCritical();
    for (size_t offset = 0; offset < rounded; offset += PAGE_SIZE)
        (void)kernel_unmap_page(base + offset);
    vPortExitCritical();
}

void memory_init(uint32_t multiboot_magic, uint32_t multiboot_info_phys)
{
    multiboot_info_t *mbi = (multiboot_info_t *)(uintptr_t)multiboot_info_phys;
    uint64_t bootstrap_start;
    uint64_t bootstrap_bytes;

    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) memory_panic("bad multiboot magic");
    if (!mbi) memory_panic("null multiboot info");

    vPortInstallKernelGDT();
    kernel_address_space.pml4_phys = (uint64_t)(uintptr_t)bootstrap_pml4;
    kernel_address_space.mappings = NULL;
    kernel_address_space.flags = 0;
    kernel_address_space.references = UINT32_MAX;
    kernel_address_space.live_threads = 0;
    kernel_address_space.permanent = 1;

    parse_multiboot_memory_map(mbi);
    bootstrap_bytes = bootstrap_bytes_required(tracked_phys_limit);
    if (!find_bootstrap_region(bootstrap_bytes, &bootstrap_start))
        memory_panic("no bootstrap metadata region");
    reserve_range(bootstrap_start, bootstrap_start + bootstrap_bytes);
    bootstrap_begin(bootstrap_start, bootstrap_bytes);

    expand_physmap(tracked_phys_limit);
    allocator_init_metadata();
    bootstrap_finish();

    page_table(kernel_address_space.pml4_phys)[0] = 0;
    address_space_activate(address_space_kernel());

    console_write("ram bytes:           "); console_decimal(phys_total_ram_bytes()); console_write("\n");
    console_write("managed pages:       "); console_decimal(phys_managed_page_count()); console_write("\n");
    console_write("free managed pages:  "); console_decimal(phys_free_page_count()); console_write("\n");
}
