#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "program.h"
#include "startup.h"

extern const uint8_t taskA_image_start[], taskA_image_end[];
extern const uint8_t taskB_image_start[], taskB_image_end[];

#define TASK_A_ENTRY PROGRAM_DEFAULT_LOAD_ADDRESS
#define TASK_B_ENTRY (PROGRAM_DEFAULT_LOAD_ADDRESS + PAGE_SIZE)
#define TASK_A_STACK PROGRAM_DEFAULT_STACK_BASE
#define TASK_B_STACK (PROGRAM_DEFAULT_STACK_BASE + 2 * PAGE_SIZE)

void kernel_startup_profile(void)
{
    program_image_t image_a = { taskA_image_start, (size_t)(taskA_image_end - taskA_image_start) };
    program_image_t image_b = { taskB_image_start, (size_t)(taskB_image_end - taskB_image_start) };
    address_space_t *address_space = address_space_create(ADDRESS_SPACE_REAP_WHEN_THREADLESS);
    uintptr_t stack_a, stack_b;
    thread_t *a = NULL, *b = NULL;
    if (!address_space ||
        program_map_flat_image(address_space, &image_a, TASK_A_ENTRY) != 0 ||
        program_map_flat_image(address_space, &image_b, TASK_B_ENTRY) != 0 ||
        program_map_user_stack(address_space, TASK_A_STACK, PAGE_SIZE, &stack_a) != 0 ||
        program_map_user_stack(address_space, TASK_B_STACK, PAGE_SIZE, &stack_b) != 0) goto failed;

    thread_create_params_t params_a = {
        .entry_rip = TASK_A_ENTRY, .initial_stack_pointer = stack_a,
        .name = "shared-A", .priority = tskIDLE_PRIORITY + 1
    };
    thread_create_params_t params_b = {
        .entry_rip = TASK_B_ENTRY, .initial_stack_pointer = stack_b,
        .name = "shared-B", .priority = tskIDLE_PRIORITY + 1
    };
    a = thread_create(address_space, THREAD_PRIVILEGE_USER, &params_a);
    b = thread_create(address_space, THREAD_PRIVILEGE_USER, &params_b);
    if (!a || !b || a->address_space != b->address_space ||
        a->kernel_stack_top == b->kernel_stack_top) goto failed;
    /* Drop the constructor reference: both threads now independently retain
     * the same ephemeral address space. */
    address_space_release(address_space);
    address_space = NULL;
    if (thread_start(a) != 0 || thread_start(b) != 0) goto failed;

    console_write("two_tasks_one_space threads "); console_decimal(a->id); console_putc(' ');
    console_decimal(b->id); console_write(" shared CR3 "); console_hex(a->address_space->pml4_phys);
    console_write("\nshared code pages "); console_hex(address_space_translate(a->address_space, TASK_A_ENTRY));
    console_putc(' '); console_hex(address_space_translate(a->address_space, TASK_B_ENTRY));
    console_write("\nshared user stacks "); console_hex(stack_a); console_putc(' '); console_hex(stack_b);
    console_write("\n");
    startup_reaper();
    return;
failed:
    if (a && a->state == THREAD_STATE_READY) thread_destroy_unstarted(a);
    if (b && b->state == THREAD_STATE_READY) thread_destroy_unstarted(b);
    if (address_space) address_space_release(address_space);
    console_write("two_tasks_one_space setup failed\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
