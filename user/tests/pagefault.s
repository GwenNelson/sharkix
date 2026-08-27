.text
.code64
.global _start
_start:
    xorq %rax, %rax
    movq (%rax), %rax
    jmp _start
