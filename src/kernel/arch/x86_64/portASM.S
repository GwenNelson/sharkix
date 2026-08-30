#include "FreeRTOSConfig.h"
.text
.code64
.extern pxCurrentTCB
.extern vTaskSwitchContext
.extern xTaskIncrementTick
.extern thread_prepare_current
.extern dispatch_syscall
.extern thread_block_current
.extern thread_timer_may_preempt_current
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
 ACTIVATE_CURRENT_THREAD
 movq pxCurrentTCB(%rip), %rax
 movq (%rax), %rsp
 RESTORE_REGS
 movq (%rsp), %r11
 addq $24, %rsp
 jmp *%r11

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
 testb $3, 8(%rsp)
 jnz 1f
 movq (%rsp), %r11
 addq $24, %rsp
 jmp *%r11
1:
 iretq

.global vPortTimerHandler
vPortTimerHandler:
 cld
 SAVE_REGS
 movq pxCurrentTCB(%rip), %rax
 movq %rsp, (%rax)
 call xTaskIncrementTick
 testl %eax, %eax
 je 1f
 call thread_timer_may_preempt_current
 testl %eax, %eax
 je 1f
 call vTaskSwitchContext
1: movb $0x20, %al
 outb %al, $0x20
 ACTIVATE_CURRENT_THREAD
 movq pxCurrentTCB(%rip), %rax
 movq (%rax), %rsp
 RESTORE_REGS
 testb $3, 8(%rsp)
 jnz 1f
 movq (%rsp), %r11
 addq $24, %rsp
 jmp *%r11
1:
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
 call thread_handle_exception
1:
 cld
 SAVE_REGS
 movq $6, %rdi
 movq 120(%rsp), %rsi
 xorq %rdx, %rdx
 xorq %rcx, %rcx
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
 call thread_handle_exception
1:
 cld
 SAVE_REGS
 movq $14, %rdi
 movq 128(%rsp), %rsi
 movq 120(%rsp), %rdx
 movq %cr2, %rcx
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
 call thread_handle_exception
1:
 cld
 SAVE_REGS
 movq $13, %rdi
 movq 128(%rsp), %rsi
 movq 120(%rsp), %rdx
 xorq %rcx, %rcx
 call thread_handle_kernel_exception

.global vPortSyscallEntry
vPortSyscallEntry:
 /* This single-core substrate uses RIP-relative cpu0 accesses.  GS is left
 * untouched until user TLS/SMP need the SWAPGS convention. */
 movq %rsp, cpu0+16(%rip)
 movq cpu0+8(%rip), %rsp
 pushq %r11; pushq %rcx; pushq %rax; pushq %rdi; pushq %rsi; pushq %rdx; pushq %r10
 pushq %r8; pushq %r9; pushq %rbx; pushq %rbp; pushq %r12; pushq %r13; pushq %r14; pushq %r15
 /* SysV AMD64: syscall_result_t is returned through a hidden first pointer
  * argument; the 56-byte by-value syscall_ctx_t is passed on the stack.
  * After the register saves RSP is 8 mod 16, so subtracting 128 leaves the
  * required 8-byte pre-call alignment.  This frame remains resident while a
  * blocked task is suspended. */
 subq $128, %rsp
 movq 224(%rsp), %rax; movq %rax, 0(%rsp)
 movq 216(%rsp), %rax; movq %rax, 8(%rsp)
 movq 208(%rsp), %rax; movq %rax, 16(%rsp)
 movq 200(%rsp), %rax; movq %rax, 24(%rsp)
 movq 192(%rsp), %rax; movq %rax, 32(%rsp)
 movq 184(%rsp), %rax; movq %rax, 40(%rsp)
 movq 176(%rsp), %rax; movq %rax, 48(%rsp)
 leaq 64(%rsp), %rdi
 call dispatch_syscall
 cmpl $0, 64(%rsp)
 je 1f
 /* BLOCK: result.ctx is the persistent return context in this thread's
  * syscall frame. thread_block_current() does not return until thread_wake(). */
 leaq 72(%rsp), %rdi
 call thread_block_current
 testl %eax, %eax
 jne vPortDefaultHandler
1:
 movq 72(%rsp), %rax; movq %rax, 224(%rsp)
 movq 80(%rsp), %rax; movq %rax, 216(%rsp)
 movq 88(%rsp), %rax; movq %rax, 208(%rsp)
 movq 96(%rsp), %rax; movq %rax, 200(%rsp)
 movq 104(%rsp), %rax; movq %rax, 192(%rsp)
 movq 112(%rsp), %rax; movq %rax, 184(%rsp)
 movq 120(%rsp), %rax; movq %rax, 176(%rsp)
 addq $128, %rsp
 popq %r15; popq %r14; popq %r13; popq %r12; popq %rbp; popq %rbx; popq %r9; popq %r8
 popq %r10; popq %rdx; popq %rsi; popq %rdi; popq %rax; popq %rcx; popq %r11
 movq cpu0+16(%rip), %rsp
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
