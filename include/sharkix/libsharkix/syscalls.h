#pragma once

#include <stddef.h>
#include <stdint.h>

#define SHARKIX_CAP_NAME_MAX 32

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

sharkix_syscall_regs_t* sharkix_syscall(sharkix_syscall_regs_t *regs);

int sharkix_cap_set_name(uint64_t cap, const char *name, size_t len);
int sharkix_cap_get_name(uint64_t cap, char *name_out, size_t out_size,
                         size_t *out_len);
