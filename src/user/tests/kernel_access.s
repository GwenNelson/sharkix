.text
.code64
.global _start
_start:
    movabs $0xffffffff80000000, %rax
    movq (%rax), %rax
    jmp _start
