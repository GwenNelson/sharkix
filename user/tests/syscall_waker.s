.text
.code64
.global _start
_start:
    movq $3, %rax
    syscall
    testq %rax, %rax
    jz 1f
    int $0x21
    jmp _start
1:  movq $0, %rax
    movq $'B', %rdi
    syscall
    movq $1, %rax
    syscall
2:  jmp 2b
