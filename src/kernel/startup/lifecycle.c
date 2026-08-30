#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "memory.h"
#include "program.h"
#include "startup.h"
#include "thread.h"

extern const uint8_t exit_image_start[], exit_image_end[];
static void lifecycle_task(void *argument)
{
    (void)argument;
    uint64_t before = phys_pages_in_use();
    for (unsigned i = 0; i < 16; ++i) {
        uint64_t reaped = thread_reaped_count();
        program_image_t image = { exit_image_start, (size_t)(exit_image_end - exit_image_start) };
        program_start_options_t options = {
            .privilege = THREAD_PRIVILEGE_USER, .name = "exit", .priority = tskIDLE_PRIORITY + 1,
            .reap_on_exit = 1
        };
        if (program_load_and_start(&image, &options, NULL, NULL) != 0) break;
        while (thread_reaped_count() == reaped) taskYIELD();
        console_putc('L');
    }
    /* An explicit output AS reference prevents an otherwise ephemeral
     * program container from disappearing with its last Thread. */
    program_image_t retained_image = { exit_image_start, (size_t)(exit_image_end - exit_image_start) };
    program_start_options_t retained_options = {
        .privilege = THREAD_PRIVILEGE_USER, .name = "held", .priority = tskIDLE_PRIORITY + 1,
        .reap_on_exit = 1
    };
    address_space_t *held_as = NULL;
    uint64_t held_reaped = thread_reaped_count();
    if (program_load_and_start(&retained_image, &retained_options, &held_as, NULL) != 0) {
        console_write(" lifecycle retained creation failed\n");
    } else {
        while (thread_reaped_count() == held_reaped) taskYIELD();
        uint64_t held_pages = phys_pages_in_use();
        address_space_release(held_as);
        console_write(" retained pages "); console_decimal(held_pages); console_putc(' ');
        console_decimal(phys_pages_in_use()); console_write("\n");
    }
    console_write(" lifecycle pages "); console_decimal(before); console_putc(' '); console_decimal(phys_pages_in_use()); console_write("\n");
    startup_kernel_spinner('K');
}
void kernel_startup_profile(void)
{
    startup_reaper();
    startup_kernel_thread(lifecycle_task, "lifecycle", tskIDLE_PRIORITY + 1);
}
