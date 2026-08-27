.section .text
.align 4
.long 0x1BADB002
.long 0x00000000
.long -(0x1BADB002 + 0x00000000)
.global _start
.extern kernel_main

_start:
    cli
    mov $stack_top, %esp
    push %ebx
    push %eax
    call kernel_main
1:
    hlt
    jmp 1b

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:
