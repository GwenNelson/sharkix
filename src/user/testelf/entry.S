.text
.code64

.global _start
.type _start, @function
.extern testelf_loader_main

_start:
    movq %rsp, %rdi
    movq 32(%rsp), %rsi
    andq $-16, %rsp
    call testelf_loader_main

1:
    pause
    jmp 1b

.size _start, .-_start
