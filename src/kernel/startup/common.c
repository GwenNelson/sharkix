#include "FreeRTOS.h"
#include "console.h"
#include "startup.h"
#include "thread.h"

static thread_t *reaper_thread;

static void reaper_task(void *argument)
{
    (void)argument;
    for (;;) { vTaskReapDeleted(); thread_reap(); taskYIELD(); }
}

thread_t *startup_kernel_thread(TaskFunction_t entry, const char *name, UBaseType_t priority)
{
    thread_create_params_t params = {
        .entry_rip = (uintptr_t)entry, .initial_stack_pointer = 0,
        .kernel_stack_size = 64 * PAGE_SIZE, .name = name, .priority = priority, .argument = NULL
    };
    return thread_create_started(address_space_kernel(), THREAD_PRIVILEGE_KERNEL, &params);
}

void startup_common_init(void)
{
    if (reaper_thread) return;
    reaper_thread = startup_kernel_thread(reaper_task, "reaper", tskIDLE_PRIORITY + 1);
    if (!reaper_thread) {
        console_write("reaper creation failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
}

void startup_reaper(void)
{
    startup_common_init();
}

void startup_kernel_spinner(char marker)
{
    for (;;) { console_putc(marker); taskYIELD(); }
}
