#include <stddef.h>
#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

#define SYS_IPC_SEND   (-202)
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

static void test_write_bytes(const char *bytes, size_t len)
{
    for (size_t i = 0; i < len; ++i)
        (void)syscall_call(SYS_TEST_WRITE, (uint64_t)(unsigned char)bytes[i], 0);
}

static void test_exit(void)
{
    (void)syscall_call(SYS_TEST_EXIT, 0, 0);
    for (;;) {
        __asm__ volatile ("pause");
    }
}

void testbin_capnames_main(const uint64_t *bootstrap)
{
    char name[SHARKIX_CAP_NAME_MAX];
    size_t name_len;
    uint64_t cap;
    static const char renamed[] = "renamed-cap";

    if (!bootstrap || bootstrap[0] < 1)
        test_exit();
    cap = bootstrap[1];

    if (sharkix_cap_get_name(cap, name, sizeof(name), &name_len) != 0)
        test_exit();
    test_write_bytes(name, name_len);
    test_write_bytes("\n", 1);

    if (sharkix_cap_set_name(cap, renamed, sizeof(renamed) - 1) != 0)
        test_exit();
    if (syscall_call(SYS_IPC_SEND, cap, 42) != 0)
        test_exit();
    test_exit();
}
