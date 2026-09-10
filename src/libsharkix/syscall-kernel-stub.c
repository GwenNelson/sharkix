#include <stdint.h>
#include <sharkix/kernel/syscall.h>
#include <sharkix/libsharkix/syscalls.h>


// we define this here, because otherwise we have to include tons of other headers from the main kernel tree
int thread_block_current(syscall_ctx_t *context);

void sharkix_syscall_raw(sharkix_syscall_regs_t *regs) {
     syscall_disposition_t disposition;

     syscall_ctx_t ctx;
 
     ctx.rax = regs->rax;
     ctx.rdi = regs->rdi;
     ctx.rsi = regs->rsi;
     ctx.rdx = regs->rdx;
     ctx.r10 = regs->r10;
     ctx.r8  = regs->r8;
     ctx.r9  = regs->r9;

     disposition = dispatch_syscall(&ctx);

     if (disposition == SYSCALL_DISPOSITION_BLOCK) {
        /*
         * dispatch_syscall() has prepared ctx as the persistent
         * return context. Block until the syscall is completed.
         */

        (void)thread_block_current(&ctx); 
     }
     regs->rax = ctx.rax;
     regs->rdi = ctx.rdi;
     regs->rsi = ctx.rsi;
     regs->rdx = ctx.rdx;
     regs->r10 = ctx.r10;
     regs->r8  = ctx.r8;
     regs->r9  = ctx.r9;

}
