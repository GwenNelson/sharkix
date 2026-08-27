#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "thread.h"
#include "syscall.h"

#define SYSCALL_TEST_WRITE  0U
#define SYSCALL_TEST_EXIT   1U
#define SYSCALL_TEST_BLOCK  2U
#define SYSCALL_TEST_WAKE   3U

static uint64_t announced_a;
static uint64_t announced_b;
static uint64_t block_test_thread_id;
static uint64_t block_test_invocation_count;
static uint64_t block_test_wake_count;

static syscall_result_t syscall_return(syscall_ctx_t ctx)
{
    syscall_result_t result = { .disposition = SYSCALL_DISPOSITION_RETURN, .ctx = ctx };
    return result;
}

static syscall_result_t syscall_block(syscall_ctx_t ctx)
{
    syscall_result_t result = { .disposition = SYSCALL_DISPOSITION_BLOCK, .ctx = ctx };
    return result;
}

/* Existing observable syscall 0: write one character and return the trusted
 * caller's SharkKernel ID in RAX. */
static syscall_result_t sys_test(syscall_ctx_t ctx)
{
    thread_t *caller = thread_current();
    if (!caller) { ctx.rax = UINT64_MAX; return syscall_return(ctx); }
    if (caller->id != announced_a && caller->id != announced_b) {
        if (!announced_a) announced_a = caller->id;
        else announced_b = caller->id;
        console_write("syscall caller thread "); console_decimal(caller->id); console_write("\n");
    }
    console_putc((char)ctx.rdi);
    ctx.rax = caller->id;
    return syscall_return(ctx);
}

/* Temporary test only: retain no policy or wait queue.  The assembly entry
 * performs the generic block after this returns BLOCK. */
static syscall_result_t sys_test_block(syscall_ctx_t ctx)
{
    thread_t *caller = thread_current();
    if (!caller || block_test_thread_id) { ctx.rax = UINT64_MAX; return syscall_return(ctx); }
    block_test_thread_id = caller->id;
    ++block_test_invocation_count;
    return syscall_block(ctx);
}

/* Temporary test only: provide every eventual register result through the
 * blocked caller's one authoritative syscall-frame context, then wake it. */
static syscall_result_t sys_test_wake(syscall_ctx_t ctx)
{
    thread_t *blocked = thread_lookup(block_test_thread_id);
    syscall_ctx_t *result;
    if (!blocked || thread_get_state(block_test_thread_id) != THREAD_STATE_BLOCKED ||
        !(result = thread_get_blocked_syscall_context(blocked))) {
        ctx.rax = UINT64_MAX;
        return syscall_return(ctx);
    }
    result->rax = 0x000000000000b10cULL;
    result->rdi = 0x1111111111111111ULL;
    result->rsi = 0x2222222222222222ULL;
    result->rdx = 0x3333333333333333ULL;
    result->r10 = 0x4444444444444444ULL;
    result->r8  = 0x5555555555555555ULL;
    result->r9  = 0x6666666666666666ULL;
    if (thread_wake(blocked) != 0) { ctx.rax = UINT64_MAX; return syscall_return(ctx); }
    ++block_test_wake_count;
    console_write("syscall_block wake issued\n");
    ctx.rax = 0;
    return syscall_return(ctx);
}

syscall_result_t dispatch_syscall(syscall_ctx_t ctx)
{
    switch (ctx.rax) {
    case SYSCALL_TEST_WRITE: return sys_test(ctx);
    case SYSCALL_TEST_EXIT: thread_exit_current();
    case SYSCALL_TEST_BLOCK: return sys_test_block(ctx);
    case SYSCALL_TEST_WAKE: return sys_test_wake(ctx);
    default:
        ctx.rax = UINT64_MAX;
        return syscall_return(ctx);
    }
}

uint64_t syscall_block_test_invocations(void) { return block_test_invocation_count; }
uint64_t syscall_block_test_wakes(void) { return block_test_wake_count; }
