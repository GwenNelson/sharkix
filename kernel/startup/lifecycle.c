#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "memory.h"
#include "startup.h"
#include "thread.h"

extern const uint8_t exit_image_start[], exit_image_end[];
static void lifecycle_task(void *argument)
{
    (void)argument;
    uint64_t before = phys_pages_in_use();
    for (unsigned i = 0; i < 16; ++i) {
        uint64_t reaped = thread_reaped_count();
        if (!thread_create_user(exit_image_start, (size_t)(exit_image_end - exit_image_start), "exit", tskIDLE_PRIORITY + 1)) break;
        while (thread_reaped_count() == reaped) taskYIELD();
        console_putc('L');
    }
    console_write(" lifecycle pages "); console_decimal(before); console_putc(' '); console_decimal(phys_pages_in_use()); console_write("\n");
    startup_kernel_spinner('K');
}
void kernel_startup_profile(void)
{
    startup_reaper();
    thread_create_kernel(lifecycle_task, "lifecycle", tskIDLE_PRIORITY + 1);
}
