#ifndef SHARKIX_ARCH_H
#define SHARKIX_ARCH_H

#include <stdint.h>
void arch_init_cpu_local(void);
void arch_init_syscalls(void);
void arch_set_kernel_stack(uintptr_t top);
void arch_install_kernel_gdt(void);
uintptr_t arch_irq_save(void);
void arch_irq_restore(uintptr_t flags);
void arch_critical_enter(void);
void arch_critical_exit(void);
void arch_interrupts_disable(void);
void arch_halt(void) __attribute__((noreturn));

#endif
