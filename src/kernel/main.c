#include <stddef.h>
#include <stdint.h>
#include "arch.h"
#include "console.h"
#include "memory.h"
#include "pmem.h"
#include "portio.h"
#include "startup.h"
#include "caps.h"
#include "ipc.h"
#include "sync.h"
#include "kvalloc.h"
#include "vmo.h"
#include "notification.h"
#include "irq.h"
#include "ipc_registry.h"
#include "as.h"

static void kernel_start_task(void *argument)
{
    (void)argument;
    console_init_late();
    kernel_startup_profile();
    thread_exit_current();
}

void kmain(void)
{
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
    knotify_init();
    kirq_init();
    kinit_caps();
    kvalloc_init();
    kvmo_init();
    kas_init();
    kvmoset_new(&(address_space_kernel()->vmoset));
    if (!startup_kernel_thread(kernel_start_task, "kernel-start", THREAD_PRIORITY_NORMAL))
        arch_halt();
    scheduler_start();
}
