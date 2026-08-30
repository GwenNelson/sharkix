#ifndef SHARKIX_MEMORY_H
#define SHARKIX_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PHYSMAP_BASE 0xffff800000000000ULL
#define KHEAP_BASE 0xffffc00000000000ULL
#define KERNEL_BASE 0xffffffff80000000ULL
#define USER_CANONICAL_TOP 0x0000800000000000ULL
#define PAGE_SIZE 4096ULL
#define PAGE_ADDR_MASK 0x000ffffffffff000ULL
#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITABLE 0x002ULL
#define PAGE_USER 0x004ULL
#define PAGE_NX (1ULL << 63)
#define ADDRESS_SPACE_MAP_OWNED (1ULL << 48)

/* The creator may drop its reference after installing initial threads.  This
 * is lifecycle intent only: normal reference counting still decides when an
 * address space is actually destroyed. */
#define ADDRESS_SPACE_REAP_WHEN_THREADLESS 0x00000001U

typedef struct address_space_mapping {
    struct address_space_mapping *next;
    uintptr_t virtual_address;
    uint64_t physical;
    unsigned owned;
} address_space_mapping_t;

typedef struct address_space {
    uint64_t pml4_phys;
    address_space_mapping_t *mappings;
    uint32_t flags;
    uint32_t references;
    uint32_t live_threads;
    uint32_t permanent;
} address_space_t;

void *phys_to_virt(uint64_t physical_address);
uint64_t virt_to_phys(const void *virtual_address);
void memory_init(uint32_t multiboot_magic, uint32_t multiboot_info_phys);
uint64_t kernel_heap_break(void);
uint64_t phys_total_ram_bytes(void);
uint64_t phys_managed_page_count(void);
uint64_t phys_free_page_count(void);
bool phys_alloc_page(uint64_t *out_page);
bool phys_alloc_pages(size_t count, uint64_t *out_page);
bool phys_alloc_page_below(uint64_t max_phys_addr, uint64_t *out_page);
bool phys_alloc_pages_below(size_t count, uint64_t max_phys_addr, uint64_t *out_page);
void phys_page_get(uint64_t page);
void phys_page_put(uint64_t page);
uint64_t phys_pages_in_use(void);
void *ksbrk(ptrdiff_t increment);
void *kmalloc(size_t size);
void kfree(void *pointer);
void *kernel_stack_alloc(size_t size);
void kernel_stack_free(void *base, size_t size);
/* The returned address space owns one reference.  The distinguished kernel
 * address space is permanent and is obtained through address_space_kernel(). */
address_space_t *address_space_create(uint32_t flags);
address_space_t *address_space_kernel(void);
void address_space_retain(address_space_t *address_space);
void address_space_release(address_space_t *address_space);
uint32_t address_space_references(const address_space_t *address_space);
/* ADDRESS_SPACE_MAP_OWNED explicitly transfers one existing caller-held
 * physical-page reference into the mapping.  Without it, a successful mapping
 * acquires its own additional page reference and the caller retains any
 * existing references it already holds. */
int address_space_map_page(address_space_t *address_space, uintptr_t va,
                           uint64_t pa, uint64_t flags);
int address_space_unmap_page(address_space_t *address_space, uintptr_t va);
void address_space_activate(address_space_t *address_space);
uint64_t address_space_translate(address_space_t *address_space, uintptr_t va);

#endif
