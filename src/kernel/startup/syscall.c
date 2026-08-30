#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "program.h"
#include "startup.h"
#include "thread.h"

extern const uint8_t taskA_image_start[], taskA_image_end[];
extern const uint8_t taskB_image_start[], taskB_image_end[];
void kernel_startup_profile(void)
{
    program_image_t image_a = { taskA_image_start, (size_t)(taskA_image_end - taskA_image_start) };
    program_image_t image_b = { taskB_image_start, (size_t)(taskB_image_end - taskB_image_start) };
    program_start_options_t options = {
        .privilege = THREAD_PRIVILEGE_USER, .name = "taskA", .priority = tskIDLE_PRIORITY + 1,
        .reap_on_exit = 1
    };
    thread_t *a, *b;
    if (program_load_and_start(&image_a, &options, NULL, &a) != 0) goto failed;
    options.name = "taskB";
    if (program_load_and_start(&image_b, &options, NULL, &b) != 0) goto failed;
    console_write("syscall threads "); console_decimal(a->id); console_putc(' '); console_decimal(b->id); console_write("\n");
    console_write("task A CR3/code/stack "); console_hex(a->address_space->pml4_phys); console_putc(' ');
    console_hex(address_space_translate(a->address_space, PROGRAM_DEFAULT_LOAD_ADDRESS)); console_putc(' '); console_hex(a->kernel_stack_top); console_write("\n");
    console_write("task B CR3/code/stack "); console_hex(b->address_space->pml4_phys); console_putc(' ');
    console_hex(address_space_translate(b->address_space, PROGRAM_DEFAULT_LOAD_ADDRESS)); console_putc(' '); console_hex(b->kernel_stack_top); console_write("\n");
    startup_reaper();
    return;
failed:
    console_write("syscall profile task creation failed\n"); for (;;) __asm__ volatile ("cli; hlt");
}
