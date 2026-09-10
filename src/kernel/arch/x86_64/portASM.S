#include "FreeRTOSConfig.h"
.text
.code64
.extern pxCurrentTCB
.extern vTaskSwitchContext
.extern xTaskIncrementTick
.extern thread_prepare_current
.extern dispatch_syscall
.extern thread_block_current
.extern thread_handle_exception
.extern thread_handle_kernel_exception
.extern thread_exit_current
.extern cpu0
.extern vPortTaskBootstrap

.macro SAVE_REGS
 pushq %rax; pushq %rbx; pushq %rcx; pushq %rdx; pushq %rbp; pushq %rsi; pushq %rdi
 pushq %r8; pushq %r9; pushq %r10; pushq %r11; pushq %r12; pushq %r13; pushq %r14; pushq %r15
.endm
.macro RESTORE_REGS
 popq %r15; popq %r14; popq %r13; popq %r12; popq %r11; popq %r10; popq %r9; popq %r8
 popq %rdi; popq %rsi; popq %rbp; popq %rdx; popq %rcx; popq %rbx; popq %rax
.endm

/* TCB field 2 is thread_t *.  Keep all Thread layout knowledge in C. */
.macro ACTIVATE_CURRENT_THREAD
 movq pxCurrentTCB(%rip), %rax
 movq 8(%rax), %rdi
 call thread_prepare_current
 movq %cr3, %rdx
 cmpq %rdx, %rax
 je 1f
 movq %rax, %cr3
1:
.endm

.global vPortStartFirstTask
vPortStartFirstTask:
 cli
 /* xPortStartScheduler called us with RSP == 8 (mod 16). */
 subq $8, %rsp
 ACTIVATE_CURRENT_THREAD
 movq pxCurrentTCB(%rip), %rax
 movq (%rax), %rsp
 RESTORE_REGS
 iretq

.global vPortYieldHandler
vPortYieldHandler:
 /* Hardware software-interrupt frame: RIP, CS, RFLAGS[, RSP, SS]. */
 cld
 SAVE_REGS
 movq pxCurrentTCB(%rip), %rax
 movq %rsp, (%rax)
 andq $-16, %rsp
 call vTaskSwitchContext
 ACTIVATE_CURRENT_THREAD
 movq pxCurrentTCB(%rip), %rax
 movq (%rax), %rsp
 RESTORE_REGS
 iretq

.global vPortTimerHandler
vPortTimerHandler:
 cld
 SAVE_REGS
 movq pxCurrentTCB(%rip), %rax
 movq %rsp, (%rax)
 andq $-16, %rsp
 call xTaskIncrementTick
 testl %eax, %eax
 je 1f
 call vTaskSwitchContext
1: movb $0x20, %al
 outb %al, $0x20
 ACTIVATE_CURRENT_THREAD
 movq pxCurrentTCB(%rip), %rax
 movq (%rax), %rsp
 RESTORE_REGS
 iretq

.global vPortInvalidOpcodeHandler
vPortInvalidOpcodeHandler:
 testb $3, 8(%rsp)
 jz 1f
 cld
 SAVE_REGS
 movq $6, %rdi
 movq 120(%rsp), %rsi
 xorq %rdx, %rdx
 xorq %rcx, %rcx
 andq $-16, %rsp
 call thread_handle_exception
1:
 cld
 SAVE_REGS
 movq $6, %rdi
 movq 120(%rsp), %rsi
 xorq %rdx, %rdx
 xorq %rcx, %rcx
 andq $-16, %rsp
 call thread_handle_kernel_exception

.global vPortPageFaultHandler
vPortPageFaultHandler:
 /* Error code precedes RIP, so CS is at RSP+16. */
 testb $3, 16(%rsp)
 jz 1f
 cld
 SAVE_REGS
 movq $14, %rdi
 movq 128(%rsp), %rsi
 movq 120(%rsp), %rdx
 movq %cr2, %rcx
 andq $-16, %rsp
 call thread_handle_exception
1:
 cld
 SAVE_REGS
 movq $14, %rdi
 movq 128(%rsp), %rsi
 movq 120(%rsp), %rdx
 movq %cr2, %rcx
 andq $-16, %rsp
 call thread_handle_kernel_exception

.global vPortGeneralProtectionHandler
vPortGeneralProtectionHandler:
 /* Error code precedes RIP, so CS is at RSP+16. */
 testb $3, 16(%rsp)
 jz 1f
 cld
 SAVE_REGS
 movq $13, %rdi
 movq 128(%rsp), %rsi
 movq 120(%rsp), %rdx
 xorq %rcx, %rcx
 andq $-16, %rsp
 call thread_handle_exception
1:
 cld
 SAVE_REGS
 movq $13, %rdi
 movq 128(%rsp), %rsi
 movq 120(%rsp), %rdx
 xorq %rcx, %rcx
 andq $-16, %rsp
 call thread_handle_kernel_exception

.global vPortSyscallEntry
vPortSyscallEntry:
 /* This single-core substrate uses RIP-relative cpu0 accesses.  GS is left
 * untouched until user TLS/SMP need the SWAPGS convention. */
 movq %rsp, cpu0+16(%rip)
 movq cpu0+8(%rip), %rsp
 pushq cpu0+16(%rip)
 pushq %r11; pushq %rcx; pushq %rax; pushq %rdi; pushq %rsi; pushq %rdx; pushq %r10
 pushq %r8; pushq %r9; pushq %rbx; pushq %rbp; pushq %r12; pushq %r13; pushq %r14; pushq %r15
 /* The 56-byte syscall_ctx_t lives in this local frame.  The saved user RSP
  * plus register saves leave RSP 16-byte aligned.  This frame remains resident
  * on the thread's kernel stack while blocked, including the syscall context
  * and architectural return RSP. */
 subq $128, %rsp
 movq 224(%rsp), %rax; movq %rax, 0(%rsp)
 movq 216(%rsp), %rax; movq %rax, 8(%rsp)
 movq 208(%rsp), %rax; movq %rax, 16(%rsp)
 movq 200(%rsp), %rax; movq %rax, 24(%rsp)
 movq 192(%rsp), %rax; movq %rax, 32(%rsp)
 movq 184(%rsp), %rax; movq %rax, 40(%rsp)
 movq 176(%rsp), %rax; movq %rax, 48(%rsp)
 leaq 0(%rsp), %rdi
 call dispatch_syscall
 testl %eax, %eax
 je 1f
 /* BLOCK: the local syscall context is persistent in this thread's syscall
  * frame. thread_block_current() does not return until thread_wake(). */
 leaq 0(%rsp), %rdi
 call thread_block_current
 testl %eax, %eax
 jne vPortDefaultHandler
1:
 movq 0(%rsp), %rax; movq %rax, 224(%rsp)
 movq 8(%rsp), %rax; movq %rax, 216(%rsp)
 movq 16(%rsp), %rax; movq %rax, 208(%rsp)
 movq 24(%rsp), %rax; movq %rax, 200(%rsp)
 movq 32(%rsp), %rax; movq %rax, 192(%rsp)
 movq 40(%rsp), %rax; movq %rax, 184(%rsp)
 movq 48(%rsp), %rax; movq %rax, 176(%rsp)
 addq $128, %rsp
 popq %r15; popq %r14; popq %r13; popq %r12; popq %rbp; popq %rbx; popq %r9; popq %r8
 popq %r10; popq %rdx; popq %rsi; popq %rdi; popq %rax; popq %rcx; popq %r11
 movq (%rsp), %rsp
 sysretq

.global vPortTaskBootstrap
vPortTaskBootstrap:
 movq %r13, %rdi
 call *%r12
 call thread_exit_current
 ud2

.global vPortDefaultHandler
vPortDefaultHandler:
 cli
1: hlt
 jmp 1b
