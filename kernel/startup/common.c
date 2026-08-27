#include "FreeRTOS.h"
#include "console.h"
#include "startup.h"
#include "thread.h"

static void reaper_task(void *argument)
{
    (void)argument;
    for (;;) { thread_reap(); vTaskReapDeleted(); taskYIELD(); }
}

void startup_reaper(void)
{
    if (!thread_create_kernel(reaper_task, "reaper", tskIDLE_PRIORITY + 1)) {
        console_write("reaper creation failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
}

void startup_kernel_spinner(char marker)
{
    for (;;) { console_putc(marker); taskYIELD(); }
}
