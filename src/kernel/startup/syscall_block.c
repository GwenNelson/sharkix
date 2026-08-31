#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "program.h"
#include "startup.h"
#include "syscall.h"
#include "thread.h"

extern const uint8_t syscall_blocker_image_start[], syscall_blocker_image_end[];
extern const uint8_t syscall_waker_image_start[], syscall_waker_image_end[];

static uint64_t blocker_id;
static uint64_t waker_id;
static uint64_t baseline_reaped;
static unsigned saw_blocker_dead;
static unsigned saw_waker_dead;

static void block_monitor(void *argument)
{
    (void)argument;
    for (;;) {
        if (thread_get_state(blocker_id) == THREAD_STATE_DEAD) saw_blocker_dead = 1;
        if (thread_get_state(waker_id) == THREAD_STATE_DEAD) saw_waker_dead = 1;
        if (syscall_block_test_invocations() == 1 && syscall_block_test_wakes() == 1 &&
            saw_blocker_dead && saw_waker_dead &&
            thread_get_state(blocker_id) == THREAD_STATE_INVALID &&
            thread_get_state(waker_id) == THREAD_STATE_INVALID &&
            thread_reaped_count() >= baseline_reaped + 2) {
            console_write("syscall_block verified: block wake one-call all-register dead-invalid reaped\n");
            for (;;) taskYIELD();
        }
        taskYIELD();
    }
}

void kernel_startup_profile(void)
{
    program_image_t blocker = { syscall_blocker_image_start,
                                (size_t)(syscall_blocker_image_end - syscall_blocker_image_start) };
    program_image_t waker = { syscall_waker_image_start,
                              (size_t)(syscall_waker_image_end - syscall_waker_image_start) };
    program_start_options_t options = {
        .privilege = THREAD_PRIVILEGE_USER, .name = "blocker",
        .priority = tskIDLE_PRIORITY + 1, .kernel_stack_size = 4 * PAGE_SIZE,
        .reap_on_exit = 1
    };
    thread_t *blocker_thread;
    thread_t *waker_thread;
    baseline_reaped = thread_reaped_count();
    if (program_load_and_start(&blocker, &options, NULL, &blocker_thread) != 0) goto failed;
    options.name = "waker";
    options.stack_base = PROGRAM_DEFAULT_STACK_BASE + 0x00200000;
    if (program_load_and_start(&waker, &options, NULL, &waker_thread) != 0) goto failed;
    blocker_id = blocker_thread->id;
    waker_id = waker_thread->id;
    console_write("syscall_block threads "); console_decimal(blocker_id); console_putc(' ');
    console_decimal(waker_id); console_write("\n");
    startup_reaper();
    if (!startup_kernel_thread(block_monitor, "block-monitor", tskIDLE_PRIORITY + 1)) goto failed;
    return;
failed:
    console_write("syscall_block setup failed\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
