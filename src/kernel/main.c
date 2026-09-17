#include <stddef.h>
#include <stdint.h>
#include "sharkix/kernel/boot/multiboot1.h"
#include "arch.h"
#include "console.h"
#include "console-serial.h"
#include "memory.h"
#include "pmem.h"
#include "portio.h"
#include "startup.h"
#include "caps.h"
#include "ipc.h"
#include "sync.h"
#include "kvalloc.h"
#include "vmo.h"
#include "irq.h"
#include "ipc_registry.h"

static void kernel_start_task(void *argument)
{
    (void)argument;
    console_init_late();
    kernel_startup_profile();
    thread_exit_current();
}

void kernel_high_entry(uint32_t magic, uint32_t info)
{
    (void)magic; (void)info;
    console_init_early();
    console_write("SharkKernel x86_64\n");
    console_write("kernel virtual base: 0xffffffff80000000\n");
    console_write("physmap base:        0xffff800000000000\n");
    console_write("kernel heap base:    0xffffc00000000000\n");
    memory_init(magic, info);
    arch_init_cpu_local();
    arch_init_syscalls();
    if (scheduler_init() != 0) {
        console_write("scheduler initialization failed\n");
        arch_halt();
    }
    startup_common_init();
    ksync_init();
    ipc_init();
    kipc_registry_init();
    kpmem_init();
    kportio_init();
    kirq_init();
    kinit_caps();
    kvalloc_init();
    kvmo_init();
    kvmoset_new(&(address_space_kernel()->vmoset));
    if (!startup_kernel_thread(kernel_start_task, "kernel-start", THREAD_PRIORITY_NORMAL))
        arch_halt();
    scheduler_start();
}
