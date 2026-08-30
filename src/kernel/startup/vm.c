#include "FreeRTOS.h"
#include "console.h"
#include "memory.h"
#include "startup.h"
#include "thread.h"

static volatile uint64_t private_as_runs;
static void vm_task(void *argument) { (void)argument; startup_kernel_spinner('V'); }
static void private_as_task(void *argument) { (void)argument; ++private_as_runs; startup_kernel_spinner('P'); }
void kernel_startup_profile(void)
{
    uint64_t before = phys_pages_in_use();
    for (unsigned i = 0; i < 32; ++i) {
        address_space_t *as = address_space_create(0);
        uint64_t page = phys_alloc_page();
        if (!as || address_space_map_page(as, 0x400000, page, PAGE_USER | ADDRESS_SPACE_MAP_OWNED) != 0) {
            console_write("vm profile failed\n"); for (;;) __asm__ volatile ("cli; hlt");
        }
        address_space_unmap_page(as, 0x400000);
        address_space_release(as);
    }
    if (phys_pages_in_use() != before) { console_write("vm profile leak\n"); for (;;) __asm__ volatile ("cli; hlt"); }
    console_write("vm profile: reclaim ok\n");
    address_space_t *private_as = address_space_create(0);
    thread_create_params_t private_params = {
        .entry_rip = (uintptr_t)private_as_task, .name = "private0",
        .priority = tskIDLE_PRIORITY + 1
    };
    thread_t *private_thread = thread_create(private_as, THREAD_PRIVILEGE_KERNEL, &private_params);
    if (!private_thread || private_as_runs ||
        thread_create(address_space_kernel(), THREAD_PRIVILEGE_USER, &private_params) != NULL) {
        console_write("vm thread model failed\n"); for (;;) __asm__ volatile ("cli; hlt");
    }
    address_space_release(private_as);
    if (thread_start(private_thread) != 0) { console_write("vm private start failed\n"); for (;;) __asm__ volatile ("cli; hlt"); }
    console_write("vm private CPL0 AS started\n");
    startup_kernel_thread(vm_task, "vm", tskIDLE_PRIORITY + 1);
}
