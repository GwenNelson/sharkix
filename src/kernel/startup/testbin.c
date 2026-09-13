#include <stddef.h>
#include <stdint.h>

#include "console.h"
#include "program.h"
#include "startup.h"

extern const uint8_t testbin_image_start[];
extern const uint8_t testbin_image_end[];

void kernel_startup_profile(void)
{
    const program_image_t image = {
        .data = testbin_image_start,
        .size = (size_t)(testbin_image_end - testbin_image_start)
    };
    const program_start_options_t options = {
        .privilege = THREAD_PRIVILEGE_USER,
        .name = "testbin",
        .priority = tskIDLE_PRIORITY + 2,
        .reap_on_exit = 1
    };

    if (program_load_and_start(&image, &options, NULL, NULL) != 0) {
        console_write("testbin startup failed\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    startup_reaper();
}
