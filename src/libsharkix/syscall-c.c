#include <stddef.h>
#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

extern void sharkix_syscall_raw(sharkix_syscall_regs_t *regs);

/*
 * Keep the assembly structure layout honest.
 */
_Static_assert(offsetof(sharkix_syscall_regs_t, rax) == 0,
               "unexpected rax offset");
_Static_assert(offsetof(sharkix_syscall_regs_t, rdi) == 8,
               "unexpected rdi offset");
_Static_assert(offsetof(sharkix_syscall_regs_t, rsi) == 16,
               "unexpected rsi offset");
_Static_assert(offsetof(sharkix_syscall_regs_t, rdx) == 24,
               "unexpected rdx offset");
_Static_assert(offsetof(sharkix_syscall_regs_t, r10) == 32,
               "unexpected r10 offset");
_Static_assert(offsetof(sharkix_syscall_regs_t, r8) == 40,
               "unexpected r8 offset");
_Static_assert(offsetof(sharkix_syscall_regs_t, r9) == 48,
               "unexpected r9 offset");

_Static_assert(sizeof(sharkix_syscall_regs_t) == 56,
               "unexpected syscall register context size");

sharkix_syscall_regs_t
sharkix_syscall(sharkix_syscall_regs_t regs)
{
    sharkix_syscall_raw(&regs);
    return regs;
}
