.text
.code64
.global _start

.equ SYS_TEST_WRITE, 0
.equ SYS_TEST_EXIT, 1
.equ SYS_IPC_SEND, -202
.equ SYS_IPC_RECV, -203
.equ SYS_VM_MAP, -301
.equ VMO_READ, 2
.equ VM_ERR_PERMISSION, -3
.equ VMUSER_VA, 0x600000
.equ PAGE_SIZE, 4096

_start:
    popq %r12                    /* VMO capability with no rights */
    popq %r13                    /* completion send capability */
    popq %r14                    /* kernel-to-B receive capability */

    movq $SYS_IPC_RECV, %rax
    movq %r14, %rdi
    syscall
    testq %rax, %rax
    jne failure

    movq %r12, %rdi
    movq $VMUSER_VA, %rsi
    xorq %rdx, %rdx
    movq $PAGE_SIZE, %r10
    movq $VMO_READ, %r8
    xorq %r9, %r9
    movq $SYS_VM_MAP, %rax
    syscall
    cmpq $VM_ERR_PERMISSION, %rax
    jne failure

    movq $SYS_TEST_WRITE, %rax
    movq $'B', %rdi
    syscall
    movq $'B', %rsi
    call report

    movq $SYS_TEST_EXIT, %rax
    xorq %rdi, %rdi
    syscall
1:
    jmp 1b

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
