#ifndef SHARKIX_ARCH_H
#define SHARKIX_ARCH_H

#include <stdint.h>
void arch_init_cpu_local(void);
void arch_init_syscalls(void);
void vPortSetKernelStack(uintptr_t top);

#endif
