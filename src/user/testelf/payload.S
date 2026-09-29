.text
.code64

.global _start
.type _start, @function

_start:
    leaq message(%rip), %rbx

1:
    movzbq (%rbx), %rdi
    testb %dil, %dil
    jz 2f
    xorl %eax, %eax
    syscall
    incq %rbx
    jmp 1b

2:
    movq $1, %rax
    syscall

3:
    pause
    jmp 3b

.size _start, .-_start

.section .rodata
message:
    .asciz "[testelf] reached ELF entry\n"
