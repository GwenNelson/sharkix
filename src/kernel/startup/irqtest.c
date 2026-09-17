#include "startup.h"
#include "thread.h"

static void irqtest_task(void *argument) { (void)argument;
	console_write("Press keys on the keyboard!");
	for(;;);

}
void kernel_startup_profile(void)
{
    startup_kernel_thread(irqtest_task, "irqtest", THREAD_PRIORITY_NORMAL);
}
