.text
.code64
.global _start
_start:
    popq %rdi
    movq $-3, %rax
    syscall
    testq %rax, %rax
    jne failure
    cmpq $0x49, %rsi
    jne failure
    movq $0, %rax
    movq $'C', %rdi
    syscall
1:  jmp 1b
failure:
    movq $0, %rax
    movq $'!', %rdi
    syscall
2:  jmp 2b
