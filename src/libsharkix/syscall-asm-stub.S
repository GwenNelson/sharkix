.text
.code64

.global sharkix_syscall_raw
.type sharkix_syscall_raw, @function

/*
 * void sharkix_syscall_raw(sharkix_syscall_regs_t *regs);
 *
 * SysV AMD64 entry:
 *     %rdi = regs
 *
 * Sharkix syscall ABI:
 *     %rax = syscall number
 *     %rdi = arg0
 *     %rsi = arg1
 *     %rdx = arg2
 *     %r10 = arg3
 *     %r8  = arg4
 *     %r9  = arg5
 *
 * On return, the values of those registers are copied back
 * into *regs.
 */

sharkix_syscall_raw:
    pushq %rdi
    movq %rdi, %rcx

    movq  0(%rcx), %rax
    movq  8(%rcx), %rdi
    movq 16(%rcx), %rsi
    movq 24(%rcx), %rdx
    movq 32(%rcx), %r10
    movq 40(%rcx), %r8
    movq 48(%rcx), %r9

    syscall

    movq (%rsp), %rcx

    movq %rax,  0(%rcx)
    movq %rdi,  8(%rcx)
    movq %rsi, 16(%rcx)
    movq %rdx, 24(%rcx)
    movq %r10, 32(%rcx)
    movq %r8,  40(%rcx)
    movq %r9,  48(%rcx)

    addq $8, %rsp
    ret

.size sharkix_syscall_raw, .-sharkix_syscall_raw
