#include "FreeRTOS.h"
#include "startup.h"
#include "thread.h"

static void normal_task(void *argument) { (void)argument; startup_kernel_spinner('K'); }
void kernel_startup_profile(void)
{
    startup_kernel_thread(normal_task, "normal", tskIDLE_PRIORITY + 1);
}
