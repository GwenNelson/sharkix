#include "FreeRTOS.h"
#include "console.h"
#include "memory.h"
#include "startup.h"
#include "thread.h"

static void vm_task(void *argument) { (void)argument; startup_kernel_spinner('V'); }
void kernel_startup_profile(void)
{
    uint64_t before = phys_pages_in_use();
    for (unsigned i = 0; i < 32; ++i) {
        address_space_t *as = address_space_create();
        uint64_t page = phys_alloc_page();
        if (!as || address_space_map_page(as, 0x400000, page, PAGE_USER | ADDRESS_SPACE_MAP_OWNED) != 0) {
            console_write("vm profile failed\n"); for (;;) __asm__ volatile ("cli; hlt");
        }
        address_space_unmap_page(as, 0x400000);
        address_space_destroy(as);
    }
    if (phys_pages_in_use() != before) { console_write("vm profile leak\n"); for (;;) __asm__ volatile ("cli; hlt"); }
    console_write("vm profile: reclaim ok\n");
    thread_create_kernel(vm_task, "vm", tskIDLE_PRIORITY + 1);
}
