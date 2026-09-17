.text
.code64
.global _start
_start:
1:  movq $0, %rax
    movq $'B', %rdi
    syscall
    int $0x90
    jmp 1b
