#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

#define SYS_TEST_WRITE 0

void _start(void)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = SYS_TEST_WRITE;
    regs.rdi = (uint64_t)'T';
    (void)sharkix_syscall(&regs);

    for (;;) {
        __asm__ volatile ("pause");
    }
}
