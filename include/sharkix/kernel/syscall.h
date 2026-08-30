#ifndef SHARKIX_SYSCALL_H
#define SHARKIX_SYSCALL_H
#include <stdint.h>


// standard syscall enum
#define SYSCALL(name, number) SYSCALL_##name = number,
typedef enum syscall_enum_t {
#include "syscalls.inc"
} syscall_enum_t;
#undef SYSCALL


/* The SharkKernel register syscall ABI only.  User return RIP/RSP/RFLAGS are
 * architectural state kept by the x86_64 syscall frame, not this structure. */
typedef struct syscall_ctx {
    uint64_t rax;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t r10;
    uint64_t r8;
    uint64_t r9;
} syscall_ctx_t;

typedef enum syscall_disposition {
    SYSCALL_DISPOSITION_RETURN,
    SYSCALL_DISPOSITION_BLOCK
} syscall_disposition_t;

typedef struct syscall_result {
    syscall_disposition_t disposition;
    syscall_ctx_t ctx;
} syscall_result_t;

syscall_result_t dispatch_syscall(syscall_ctx_t ctx);

/* Temporary, profile-only blocking-syscall test observability. */
uint64_t syscall_block_test_invocations(void);
uint64_t syscall_block_test_wakes(void);
#endif
