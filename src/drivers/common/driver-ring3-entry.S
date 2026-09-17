.text
.code64

.global _start
.type _start, @function
.extern driver_user_main

_start:
    movq %rsp, %rdi
    andq $-16, %rsp
    call driver_user_main

1:
    pause
    jmp 1b

.size _start, .-_start
