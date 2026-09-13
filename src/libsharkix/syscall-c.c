#include <stddef.h>
#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

extern void sharkix_syscall_raw(sharkix_syscall_regs_t *regs);

/*
 * Keep the assembly structure layout honest.
 */
_Static_assert(offsetof(sharkix_syscall_regs_t, rax) == 0,
               "unexpected rax offset");
_Static_assert(offsetof(sharkix_syscall_regs_t, rdi) == 8,
               "unexpected rdi offset");
_Static_assert(offsetof(sharkix_syscall_regs_t, rsi) == 16,
               "unexpected rsi offset");
_Static_assert(offsetof(sharkix_syscall_regs_t, rdx) == 24,
               "unexpected rdx offset");
_Static_assert(offsetof(sharkix_syscall_regs_t, r10) == 32,
               "unexpected r10 offset");
_Static_assert(offsetof(sharkix_syscall_regs_t, r8) == 40,
               "unexpected r8 offset");
_Static_assert(offsetof(sharkix_syscall_regs_t, r9) == 48,
               "unexpected r9 offset");

_Static_assert(sizeof(sharkix_syscall_regs_t) == 56,
               "unexpected syscall register context size");

sharkix_syscall_regs_t* sharkix_syscall(sharkix_syscall_regs_t *regs) {
    sharkix_syscall_raw(regs);
    return regs;
}

static uint64_t sharkix_pack_name_word(const char *name, size_t len,
                                       size_t offset)
{
    uint64_t word = 0;

    for (size_t i = 0; i < sizeof(word) && offset + i < len; ++i)
        word |= (uint64_t)(uint8_t)name[offset + i] << (i * 8);
    return word;
}

static void sharkix_unpack_name_word(char *name, size_t len, size_t offset,
                                     uint64_t word)
{
    for (size_t i = 0; i < sizeof(word) && offset + i < len; ++i)
        name[offset + i] = (char)(word >> (i * 8));
}

int sharkix_cap_set_name(uint64_t cap, const char *name, size_t len)
{
    sharkix_syscall_regs_t regs = { 0 };

    if (len > SHARKIX_CAP_NAME_MAX || (len != 0 && !name))
        return -1;

    regs.rdx = sharkix_pack_name_word(name, len, 0);
    regs.r10 = sharkix_pack_name_word(name, len, 8);
    regs.r8 = sharkix_pack_name_word(name, len, 16);
    regs.r9 = sharkix_pack_name_word(name, len, 24);
    regs.rax = (uint64_t)-106;
    regs.rdi = cap;
    regs.rsi = len;
    (void)sharkix_syscall(&regs);
    return regs.rax == 0 ? 0 : -1;
}

int sharkix_cap_get_name(uint64_t cap, char *name_out, size_t out_size,
                         size_t *out_len)
{
    sharkix_syscall_regs_t regs = { 0 };
    size_t len;

    if (!name_out || !out_len)
        return -1;

    regs.rax = (uint64_t)-107;
    regs.rdi = cap;
    regs.rsi = out_size;
    (void)sharkix_syscall(&regs);
    len = (size_t)regs.rsi;
    *out_len = len;
    if (regs.rax != 0 || len > SHARKIX_CAP_NAME_MAX || len > out_size)
        return -1;

    sharkix_unpack_name_word(name_out, len, 0, regs.rdx);
    sharkix_unpack_name_word(name_out, len, 8, regs.r10);
    sharkix_unpack_name_word(name_out, len, 16, regs.r8);
    sharkix_unpack_name_word(name_out, len, 24, regs.r9);
    return 0;
}
