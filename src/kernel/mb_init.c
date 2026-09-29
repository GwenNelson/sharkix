#include <stdint.h>

#include "console.h"
#include "memory.h"

void kmain(void);

void kernel_high_entry(uint32_t magic, uint32_t info)
{
    console_init_early();
    console_write("SharkKernel x86_64\n");
    console_write("kernel virtual base: 0xffffffff80000000\n");
    console_write("physmap base:        0xffff800000000000\n");
    console_write("kernel heap base:    0xffffc00000000000\n");
    memory_init(magic, info);

    kmain();

    for (;;) {
    }
}
