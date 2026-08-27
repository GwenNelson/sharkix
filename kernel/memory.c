#include <stdint.h>
#include "memory.h"

extern uint64_t bootstrap_pml4[];
extern void vPortInstallKernelGDT(void);
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

void memory_init(void)
{
    vPortInstallKernelGDT();
    uint64_t *pml4 = (uint64_t *)phys_to_virt((uint64_t)(uintptr_t)bootstrap_pml4);
    pml4[0] = 0;
    __asm__ volatile ("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
}
