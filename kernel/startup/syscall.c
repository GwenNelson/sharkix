#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "startup.h"
#include "thread.h"

extern const uint8_t taskA_image_start[], taskA_image_end[];
extern const uint8_t taskB_image_start[], taskB_image_end[];
void kernel_startup_profile(void)
{
    thread_t *a = thread_create_user(taskA_image_start, (size_t)(taskA_image_end - taskA_image_start), "taskA", tskIDLE_PRIORITY + 1);
    thread_t *b = thread_create_user(taskB_image_start, (size_t)(taskB_image_end - taskB_image_start), "taskB", tskIDLE_PRIORITY + 1);
    if (!a || !b) { console_write("syscall profile task creation failed\n"); for (;;) __asm__ volatile ("cli; hlt"); }
    console_write("syscall threads "); console_decimal(a->id); console_putc(' '); console_decimal(b->id); console_write("\n");
    console_write("task A CR3/code/stack "); console_hex(a->address_space->pml4_phys); console_putc(' ');
    console_hex(address_space_translate(a->address_space, USER_CODE_BASE)); console_putc(' '); console_hex(a->kernel_stack_top); console_write("\n");
    console_write("task B CR3/code/stack "); console_hex(b->address_space->pml4_phys); console_putc(' ');
    console_hex(address_space_translate(b->address_space, USER_CODE_BASE)); console_putc(' '); console_hex(b->kernel_stack_top); console_write("\n");
    startup_reaper();
}
