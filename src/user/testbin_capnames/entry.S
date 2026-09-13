.text
.code64

.global _start
.type _start, @function
.extern testbin_capnames_main

_start:
    movq %rsp, %rdi
    andq $-16, %rsp
    call testbin_capnames_main

1:
    pause
    jmp 1b

.size _start, .-_start
