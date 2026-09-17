.text
.code64
.extern scheduler_on_yield
.extern scheduler_on_tick
.extern dispatch_syscall
.extern thread_block_current
.extern thread_handle_exception
.extern thread_handle_kernel_exception
.extern thread_exit_current
.extern cpu0

.macro SAVE_REGS
 pushq %rax; pushq %rbx; pushq %rcx; pushq %rdx; pushq %rbp; pushq %rsi; pushq %rdi
 pushq %r8; pushq %r9; pushq %r10; pushq %r11; pushq %r12; pushq %r13; pushq %r14; pushq %r15
.endm
.macro RESTORE_REGS
 popq %r15; popq %r14; popq %r13; popq %r12; popq %r11; popq %r10; popq %r9; popq %r8
 popq %rdi; popq %rsi; popq %rbp; popq %rdx; popq %rcx; popq %rbx; popq %rax
.endm

.global arch_start_first_thread
arch_start_first_thread:
 cli
 movq %rdi, %rsp
 RESTORE_REGS
 iretq

.global arch_yield_handler
arch_yield_handler:
 /* Hardware software-interrupt frame: RIP, CS, RFLAGS[, RSP, SS]. */
 cld
 SAVE_REGS
 movq %rsp, %rdi
 andq $-16, %rsp
 call scheduler_on_yield
 movq %rax, %rsp
 RESTORE_REGS
 iretq

.global arch_timer_handler
arch_timer_handler:
 cld
 SAVE_REGS
 movq %rsp, %rdi
 movb $0x20, %al
 outb %al, $0x20
 andq $-16, %rsp
 call scheduler_on_tick
 movq %rax, %rsp
 RESTORE_REGS
 iretq

.global arch_invalid_opcode_handler
arch_invalid_opcode_handler:
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

.global arch_page_fault_handler
arch_page_fault_handler:
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

.global arch_general_protection_handler
arch_general_protection_handler:
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

.global arch_syscall_entry
arch_syscall_entry:
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
 jne arch_default_handler
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

.global arch_thread_bootstrap
arch_thread_bootstrap:
 movq %r13, %rdi
 call *%r12
 call thread_exit_current
 ud2

.global arch_default_handler
arch_default_handler:
 cli
1: hlt
 jmp 1b
