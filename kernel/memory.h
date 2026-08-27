#ifndef SHARKIX_MEMORY_H
#define SHARKIX_MEMORY_H
#include <stdint.h>
#define PHYSMAP_BASE 0xffff800000000000ULL
#define KHEAP_BASE 0xffffc00000000000ULL
#define KERNEL_BASE 0xffffffff80000000ULL
void *phys_to_virt(uint64_t physical_address);
uint64_t virt_to_phys(const void *virtual_address);
void memory_init(void);
uint64_t kernel_heap_break(void);
#endif
