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
#include "ipc.h"
#include "sync.h"
#include "kvalloc.h"
#include "vmo.h"

void vApplicationMallocFailedHook(void) { for (;;) __asm__ volatile ("cli; hlt"); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) { (void)task; (void)name; for (;;) __asm__ volatile ("cli; hlt"); }

static void kernel_start_task(void *argument)
{
    (void)argument;
    console_vga_init();
    kernel_startup_profile();
    thread_exit_current();
}

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
    ipc_init();
    kpmem_init();
    kinit_caps();
    kvalloc_init();
    kvmo_init();
    kvmoset_new(&(address_space_kernel()->vmoset));
    if (!startup_kernel_thread(kernel_start_task, "kernel-start", tskIDLE_PRIORITY + 2))
        for (;;) __asm__ volatile ("cli; hlt");
    vTaskStartScheduler();
    for (;;) __asm__ volatile ("cli; hlt");
}
