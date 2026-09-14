.text
.code64

.global _start
.type _start, @function
.extern serial_consoled_main

_start:
    movq %rsp, %rdi
    andq $-16, %rsp
    call serial_consoled_main

1:
    pause
    jmp 1b

.size _start, .-_start
