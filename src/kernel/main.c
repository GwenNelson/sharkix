#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "sharkix/kernel/boot/multiboot1.h"
#include "task.h"
#include "arch.h"
#include "console.h"
#include "console-serial.h"
#include "console-vga.h"
#include "memory.h"
#include "pmem.h"
#include "startup.h"
#include "caps.h"
#include "sync.h"

void vApplicationMallocFailedHook(void) { for (;;) __asm__ volatile ("cli; hlt"); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) { (void)task; (void)name; for (;;) __asm__ volatile ("cli; hlt"); }

void kernel_high_entry(uint32_t magic, uint32_t info)
{
    (void)magic; (void)info;
    console_serial_init();
    console_write("SharkKernel x86_64\n");
    console_write("kernel virtual base: 0xffffffff80000000\n");
    console_write("physmap base:        0xffff800000000000\n");
    console_write("kernel heap base:    0xffffc00000000000\n");
    memory_init(magic, info);
    arch_init_cpu_local();
    arch_init_syscalls();
    startup_common_init();
    ksync_init();
    kpmem_init();
    kinit_caps();
    console_vga_init();
    kernel_startup_profile();
    vTaskStartScheduler();
    for (;;) __asm__ volatile ("cli; hlt");
}
