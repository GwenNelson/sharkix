#ifndef SHARKIX_MEMORY_H
#define SHARKIX_MEMORY_H

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

typedef struct address_space_mapping {
    struct address_space_mapping *next;
    uintptr_t virtual_address;
    uint64_t physical;
    unsigned owned;
} address_space_mapping_t;

typedef struct address_space {
    uint64_t pml4_phys;
    address_space_mapping_t *mappings;
} address_space_t;

extern address_space_t kernel_address_space;
void *phys_to_virt(uint64_t physical_address);
uint64_t virt_to_phys(const void *virtual_address);
void memory_init(void);
uint64_t kernel_heap_break(void);
uint64_t phys_alloc_page(void);
void phys_free_page(uint64_t page);
uint64_t phys_pages_in_use(void);
address_space_t *address_space_create(void);
void address_space_destroy(address_space_t *address_space);
int address_space_map_page(address_space_t *address_space, uintptr_t va,
                           uint64_t pa, uint64_t flags);
int address_space_unmap_page(address_space_t *address_space, uintptr_t va);
void address_space_activate(address_space_t *address_space);
uint64_t address_space_translate(address_space_t *address_space, uintptr_t va);

#endif
