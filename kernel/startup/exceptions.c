#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "program.h"
#include "startup.h"
#include "thread.h"

extern const uint8_t ud_image_start[], ud_image_end[];
extern const uint8_t pagefault_image_start[], pagefault_image_end[];
extern const uint8_t kernel_access_image_start[], kernel_access_image_end[];
static void survivor_task(void *argument) { (void)argument; startup_kernel_spinner('E'); }
void kernel_startup_profile(void)
{
    startup_reaper();
    program_start_options_t options = { .privilege = THREAD_PRIVILEGE_USER, .priority = tskIDLE_PRIORITY + 1, .reap_on_exit = 1 };
    program_image_t image = { ud_image_start, (size_t)(ud_image_end - ud_image_start) };
    options.name = "ud"; program_load_and_start(&image, &options, NULL, NULL);
    image.data = pagefault_image_start; image.size = (size_t)(pagefault_image_end - pagefault_image_start);
    options.name = "pf"; program_load_and_start(&image, &options, NULL, NULL);
    image.data = kernel_access_image_start; image.size = (size_t)(kernel_access_image_end - kernel_access_image_start);
    options.name = "kpf"; program_load_and_start(&image, &options, NULL, NULL);
    startup_kernel_thread(survivor_task, "survivor", tskIDLE_PRIORITY + 1);
    console_write("exceptions profile started\n");
}
