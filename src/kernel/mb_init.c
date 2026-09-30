#include <stdint.h>

#include "console.h"
#include "memory.h"

#define STRINGIFY_VALUE_(value) #value
#define STRINGIFY_VALUE(value) STRINGIFY_VALUE_(value)

void kmain(void);

void kernel_high_entry(uint32_t magic, uint32_t info)
{
    console_init_early();
    console_write("Sharkix MicroKernel " STRINGIFY_VALUE(SHARKIX_ARCH) " "
                  STRINGIFY_VALUE(SHARKIX_ARCH_BITS) "-bit\n");
    console_write("kernel virtual base: "); console_hex(KERNEL_BASE); console_write("\n");
    console_write("physmap base:        "); console_hex(PHYSMAP_BASE); console_write("\n");
    console_write("kernel heap base:    "); console_hex(KHEAP_BASE); console_write("\n");
    memory_init(magic, info);

    kmain();

    for (;;) {
    }
}
