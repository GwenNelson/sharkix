#include "FreeRTOSConfig.h"
.text
.code64
.extern pxCurrentTCB
.extern kernel_address_space
.extern vTaskSwitchContext
.extern xTaskIncrementTick
.extern thread_prepare_current
.extern dispatch_syscall
.extern thread_handle_exception
.extern cpu0

.macro SAVE_REGS
 pushq %rax; pushq %rbx; pushq %rcx; pushq %rdx; pushq %rbp; pushq %rsi; pushq %rdi
 pushq %r8; pushq %r9; pushq %r10; pushq %r11; pushq %r12; pushq %r13; pushq %r14; pushq %r15
.endm
.macro RESTORE_REGS
 popq %r15; popq %r14; popq %r13; popq %r12; popq %r11; popq %r10; popq %r9; popq %r8
 popq %rdi; popq %rsi; popq %rbp; popq %rdx; popq %rcx; popq %rbx; popq %rax
.endm

/* TCB field 2 is thread_t *.  thread_t->address_space is field 2. */
.macro ACTIVATE_CURRENT_THREAD
 movq pxCurrentTCB(%rip), %rax
 movq 8(%rax), %rdi
 movq %rdi, %rbx
 call thread_prepare_current
 testq %rbx, %rbx
 jnz 1f
 leaq kernel_address_space(%rip), %rax
 jmp 2f
1: movq 8(%rbx), %rax
2: movq (%rax), %rax
 movq %cr3, %rdx
 cmpq %rdx, %rax
 je 3f
 movq %rax, %cr3
3:
.endm

/* On a CPL3 return frame CS is at RSP+8.  Kernel frames retain the previous
 * direct-jump convention so the pre-existing kernel FreeRTOS ABI is unchanged. */
.macro RETURN_CONTEXT kernel_skip
 testb $3, 8(%rsp)
 jz 1f
 iretq
1: movq (%rsp), %r11
 addq $\kernel_skip, %rsp
 sti
 jmp *%r11
.endm

.global vPortStartFirstTask
vPortStartFirstTask:
 ACTIVATE_CURRENT_THREAD
 movq pxCurrentTCB(%rip), %rax
 movq (%rax), %rsp
 RESTORE_REGS
 RETURN_CONTEXT 24

.global vPortYieldHandler
vPortYieldHandler:
 /* Hardware software-interrupt frame: RIP, CS, RFLAGS[, RSP, SS]. */
 cld
 SAVE_REGS
 movq pxCurrentTCB(%rip), %rax
 movq %rsp, (%rax)
 call vTaskSwitchContext
 ACTIVATE_CURRENT_THREAD
 movq pxCurrentTCB(%rip), %rax
 movq (%rax), %rsp
 RESTORE_REGS
 RETURN_CONTEXT 40

.global vPortTimerHandler
vPortTimerHandler:
 cld
 SAVE_REGS
 movq pxCurrentTCB(%rip), %rax
 movq %rsp, (%rax)
 call xTaskIncrementTick
 testl %eax, %eax
 je 2f
 call vTaskSwitchContext
2: movb $0x20, %al
 outb %al, $0x20
 ACTIVATE_CURRENT_THREAD
 movq pxCurrentTCB(%rip), %rax
 movq (%rax), %rsp
 RESTORE_REGS
 RETURN_CONTEXT 40

.global vPortInvalidOpcodeHandler
vPortInvalidOpcodeHandler:
 testb $3, 8(%rsp)
 jz vPortDefaultHandler
 cld
 SAVE_REGS
 movq $6, %rdi
 movq 120(%rsp), %rsi
 xorq %rdx, %rdx
 xorq %rcx, %rcx
 call thread_handle_exception

.global vPortPageFaultHandler
vPortPageFaultHandler:
 /* Error code precedes RIP, so CS is at RSP+16. */
 testb $3, 16(%rsp)
 jz vPortDefaultHandler
 cld
 SAVE_REGS
 movq $14, %rdi
 movq 128(%rsp), %rsi
 movq 120(%rsp), %rdx
 movq %cr2, %rcx
 call thread_handle_exception

.global vPortSyscallEntry
vPortSyscallEntry:
 /* This single-core substrate uses RIP-relative cpu0 accesses.  GS is left
 * untouched until user TLS/SMP need the SWAPGS convention. */
 movq %rsp, cpu0+16(%rip)
 movq cpu0+8(%rip), %rsp
 pushq %r11; pushq %rcx; pushq %rax; pushq %rdi; pushq %rsi; pushq %rdx; pushq %r10
 pushq %r8; pushq %r9; pushq %rbx; pushq %rbp; pushq %r12; pushq %r13; pushq %r14; pushq %r15
 /* Saved layout: r15..r8, r10, rdx, rsi, rdi, rax, rcx, r11. */
 movq 48(%rsp), %rax
 subq $8, %rsp
 pushq %rax
 movq 112(%rsp), %rdi
 movq 104(%rsp), %rsi
 movq 96(%rsp), %rdx
 movq 88(%rsp), %rcx
 movq 80(%rsp), %r8
 movq 72(%rsp), %r9
 call dispatch_syscall
 addq $16, %rsp
 movq %rax, 96(%rsp)
 popq %r15; popq %r14; popq %r13; popq %r12; popq %rbp; popq %rbx; popq %r9; popq %r8
 popq %r10; popq %rdx; popq %rsi; popq %rdi; popq %rax; popq %rcx; popq %r11
 movq cpu0+16(%rip), %rsp
 sysretq

.global vPortDefaultHandler
vPortDefaultHandler:
 cli
1: hlt
 jmp 1b
