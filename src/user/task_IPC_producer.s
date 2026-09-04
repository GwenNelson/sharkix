.text
.code64
.global _start
_start:
    popq %rdi
    movq $0x49, %rsi
    xorq %rdx, %rdx
    xorq %r10, %r10
    xorq %r8, %r8
    xorq %r9, %r9
    movq $-2, %rax
    syscall
    testq %rax, %rax
    jne failure
    movq $0, %rax
    movq $'P', %rdi
    syscall
1:  jmp 1b
failure:
    movq $0, %rax
    movq $'!', %rdi
    syscall
2:  jmp 2b
