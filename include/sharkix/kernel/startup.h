#ifndef SHARKIX_STARTUP_H
#define SHARKIX_STARTUP_H

#include "thread.h"

void kernel_startup_profile(void);
void startup_common_init(void);
void startup_reaper(void);
void startup_kernel_spinner(char marker);
thread_t *startup_kernel_thread(TaskFunction_t entry, const char *name, UBaseType_t priority);

#endif
