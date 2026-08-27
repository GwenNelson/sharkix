#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "startup.h"
#include "thread.h"

extern const uint8_t ud_image_start[], ud_image_end[];
extern const uint8_t pagefault_image_start[], pagefault_image_end[];
extern const uint8_t kernel_access_image_start[], kernel_access_image_end[];
static void survivor_task(void *argument) { (void)argument; startup_kernel_spinner('E'); }
void kernel_startup_profile(void)
{
    startup_reaper();
    thread_create_user(ud_image_start, (size_t)(ud_image_end - ud_image_start), "ud", tskIDLE_PRIORITY + 1);
    thread_create_user(pagefault_image_start, (size_t)(pagefault_image_end - pagefault_image_start), "pf", tskIDLE_PRIORITY + 1);
    thread_create_user(kernel_access_image_start, (size_t)(kernel_access_image_end - kernel_access_image_start), "kpf", tskIDLE_PRIORITY + 1);
    thread_create_kernel(survivor_task, "survivor", tskIDLE_PRIORITY + 1);
    console_write("exceptions profile started\n");
}
