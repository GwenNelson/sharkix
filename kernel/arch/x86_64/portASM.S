#include "FreeRTOSConfig.h"
.text
.code64
.extern pxCurrentTCB
.extern kernel_address_space
.extern vTaskSwitchContext
.extern xTaskIncrementTick
.macro SAVE_REGS
 pushq %rax; pushq %rbx; pushq %rcx; pushq %rdx; pushq %rbp; pushq %rsi; pushq %rdi
 pushq %r8; pushq %r9; pushq %r10; pushq %r11; pushq %r12; pushq %r13; pushq %r14; pushq %r15
.endm
.macro RESTORE_REGS
 popq %r15; popq %r14; popq %r13; popq %r12; popq %r11; popq %r10; popq %r9; popq %r8
 popq %rdi; popq %rsi; popq %rbp; popq %rdx; popq %rcx; popq %rbx; popq %rax
.endm
.macro ACTIVATE_CURRENT_ADDRESS_SPACE
 movq pxCurrentTCB(%rip), %rax
 movq 8(%rax), %rcx
 testq %rcx, %rcx
 jnz 1f
 leaq kernel_address_space(%rip), %rcx
1: movq (%rcx), %rcx
 movq %cr3, %rdx
 cmpq %rdx, %rcx
 je 2f
 movq %rcx, %cr3
2:
.endm
.global vPortStartFirstTask
vPortStartFirstTask:
 ACTIVATE_CURRENT_ADDRESS_SPACE; movq (%rax), %rsp; RESTORE_REGS
 movq (%rsp), %r11; addq $24, %rsp; sti; jmp *%r11
.global vPortYieldHandler
vPortYieldHandler:
 cld; SAVE_REGS; movq pxCurrentTCB(%rip), %rax; movq %rsp, (%rax); call vTaskSwitchContext
 ACTIVATE_CURRENT_ADDRESS_SPACE; movq (%rax), %rsp; RESTORE_REGS; movq (%rsp), %r11; addq $40, %rsp; sti; jmp *%r11
.global vPortTimerHandler
vPortTimerHandler:
 cld; SAVE_REGS; movq pxCurrentTCB(%rip), %rax; movq %rsp, (%rax); call xTaskIncrementTick
 testl %eax, %eax; je 1f; call vTaskSwitchContext
1: movb $0x20, %al; outb %al, $0x20; ACTIVATE_CURRENT_ADDRESS_SPACE; movq (%rax), %rsp; RESTORE_REGS
 movq (%rsp), %r11; addq $40, %rsp; sti; jmp *%r11
.global vPortDefaultHandler
vPortDefaultHandler: cli; 2: hlt; jmp 2b
.global vPortPageFaultHandler
vPortPageFaultHandler: cli; 3: hlt; jmp 3b
