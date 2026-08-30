.text
.code64
.global _start
_start:
1:  movq $0, %rax
    movq $'A', %rdi
    syscall
    int $0x21
    jmp 1b
