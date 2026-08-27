#ifndef SHARKIX_MEMORY_H
#define SHARKIX_MEMORY_H
#include <stdint.h>
#define PHYSMAP_BASE 0xffff800000000000ULL
#define KHEAP_BASE 0xffffc00000000000ULL
#define KERNEL_BASE 0xffffffff80000000ULL
#define PAGE_SIZE 4096ULL
#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITABLE 0x002ULL

typedef struct address_space {
    uint64_t pml4_phys;
} address_space_t;

extern address_space_t kernel_address_space;

void *phys_to_virt(uint64_t physical_address);
uint64_t virt_to_phys(const void *virtual_address);
void memory_init(void);
uint64_t kernel_heap_break(void);
uint64_t phys_alloc_page(void);
address_space_t *address_space_create(void);
void address_space_destroy(address_space_t *address_space);
int address_space_map_page(address_space_t *address_space, uintptr_t va,
                           uintptr_t pa, unsigned flags);
void address_space_activate(address_space_t *address_space);
uint64_t address_space_translate(address_space_t *address_space, uintptr_t va);
#endif
