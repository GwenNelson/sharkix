.text
.code64
.global _start

.equ SYS_TEST_WRITE, 0
.equ SYS_TEST_EXIT, 1
.equ SYS_IPC_SEND, -202
.equ SYS_IPC_RECV, -203
.equ SYS_VM_MAP, -301
.equ SYS_VM_UNMAP, -302
.equ VMO_READ, 2
.equ VMUSER_VA, 0x600000
.equ PAGE_SIZE, 4096

_start:
    popq %r12                    /* VMO capability */
    popq %r13                    /* completion send capability */
    popq %r14                    /* kernel-to-A receive capability */

    movq %r12, %rdi
    movq $VMUSER_VA, %rsi
    xorq %rdx, %rdx
    movq $PAGE_SIZE, %r10
    movq $VMO_READ, %r8
    xorq %r9, %r9
    movq $SYS_VM_MAP, %rax
    syscall
    testq %rax, %rax
    jne failure

    movq $SYS_TEST_WRITE, %rax
    movq $'A', %rdi
    syscall
    movq $'A', %rsi
    call report

    call wait_for_kernel

    movq %r12, %rdi
    movq $VMUSER_VA, %rsi
    movq $SYS_VM_UNMAP, %rax
    syscall
    testq %rax, %rax
    jne failure

    movq $'U', %rsi
    call report
    call wait_for_kernel

    movq $VMUSER_VA, %r15
    movq (%r15), %rax            /* Expected user-mode page fault. */
1:
    jmp 1b

wait_for_kernel:
    movq $SYS_IPC_RECV, %rax
    movq %r14, %rdi
    syscall
    testq %rax, %rax
    jne failure
    ret

report:
    movq $SYS_IPC_SEND, %rax
    movq %r13, %rdi
    xorq %rdx, %rdx
    xorq %r10, %r10
    xorq %r8, %r8
    xorq %r9, %r9
    syscall
    testq %rax, %rax
    jne failure
    ret

failure:
    movq $SYS_TEST_WRITE, %rax
    movq $'!', %rdi
    syscall
    movq $'!', %rsi
    movq $SYS_IPC_SEND, %rax
    movq %r13, %rdi
    xorq %rdx, %rdx
    xorq %r10, %r10
    xorq %r8, %r8
    xorq %r9, %r9
    syscall
    movq $SYS_TEST_EXIT, %rax
    xorq %rdi, %rdi
    syscall
2:
    jmp 2b
