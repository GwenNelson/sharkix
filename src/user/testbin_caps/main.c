#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

#define SYS_IPC_SEND  (-202)
#define SYS_IPC_RECV  (-203)
#define SYS_TEST_WRITE 0
#define SYS_TEST_EXIT  1

static uint64_t syscall_call(uint64_t number, uint64_t arg0, uint64_t arg1)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = number;
    regs.rdi = arg0;
    regs.rsi = arg1;
    (void)sharkix_syscall(&regs);
    return regs.rax;
}

static uint64_t ipc_receive(uint64_t endpoint)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = SYS_IPC_RECV;
    regs.rdi = endpoint;
    (void)sharkix_syscall(&regs);
    if (regs.rax != 0)
        return UINT64_MAX;
    return regs.rsi;
}

static void test_exit(void)
{
    (void)syscall_call(SYS_TEST_EXIT, 0, 0);
    for (;;) {
        __asm__ volatile ("pause");
    }
}

void testbin_caps_main(const uint64_t *bootstrap)
{
    uint64_t count;
    uint64_t endpoint1;
    uint64_t endpoint2;
    uint64_t endpoint3;
    uint64_t received;

    if (!bootstrap)
        test_exit();

    count = bootstrap[0];
    if (count < 3)
        test_exit();

    endpoint1 = bootstrap[1];
    endpoint2 = bootstrap[2];
    endpoint3 = bootstrap[3];

    /* Send A to the kernel worker on the first, send-only capability. */
    if (syscall_call(SYS_IPC_SEND, endpoint1, (uint64_t)'A') != 0)
        test_exit();

    /* Receive B from the kernel worker on the second, receive-only cap. */
    received = ipc_receive(endpoint2);
    if (received != (uint64_t)'B')
        test_exit();
    (void)syscall_call(SYS_TEST_WRITE, (uint64_t)'B', 0);

    /* Complete the protocol through the third, send-only capability. */
    if (syscall_call(SYS_IPC_SEND, endpoint3, (uint64_t)'C') != 0)
        test_exit();

    test_exit();
}
