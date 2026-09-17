#ifndef SHARKIX_ARCH_H
#define SHARKIX_ARCH_H

#include <stdint.h>
#include "scheduler.h"

void arch_init_cpu_local(void);
void arch_init_syscalls(void);
void arch_set_kernel_stack(uintptr_t top);
void arch_install_kernel_gdt(void);
uintptr_t arch_thread_context_init(uintptr_t kernel_stack_top,
                                   thread_entry_t entry, void *argument,
                                   int user, uintptr_t user_stack);
void arch_scheduler_start(uintptr_t saved_context) __attribute__((noreturn));
void arch_scheduler_yield(void);
void arch_wait_for_interrupt(void);
uintptr_t arch_irq_save(void);
void arch_irq_restore(uintptr_t flags);
void arch_critical_enter(void);
void arch_critical_exit(void);
void arch_interrupts_disable(void);
void arch_halt(void) __attribute__((noreturn));

#endif
