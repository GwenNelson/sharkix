#pragma once

#include <stdint.h>

// TODO - at some point i really need to start giving a crap about portability

typedef struct sharkix_syscall_regs {
    uint64_t rax;    /* syscall number / return */
    uint64_t rdi;    /* arg0 */
    uint64_t rsi;    /* arg1 */
    uint64_t rdx;    /* arg2 */
    uint64_t r10;    /* arg3 */
    uint64_t r8;     /* arg4 */
    uint64_t r9;     /* arg5 */
} sharkix_syscall_regs_t;

sharkix_syscall_regs_t
sharkix_syscall(sharkix_syscall_regs_t regs);
