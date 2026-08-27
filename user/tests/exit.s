.text
.code64
.global _start
_start:
    movq $1, %rax
    syscall
    jmp _start
