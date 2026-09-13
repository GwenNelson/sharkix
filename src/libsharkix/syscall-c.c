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

void sharkix_debug_puts(const char *s)
{
    if (!s)
        return;

    while (*s) {
        sharkix_syscall_regs_t regs = {
            .rax = 0,
            .rdi = (uint64_t)(unsigned char)*s
        };

        (void)sharkix_syscall(&regs);
        ++s;
    }
}

static size_t sharkix_bootstrap_name_length(const char *name)
{
    size_t len;

    for (len = 0; len <= SHARKIX_CAP_NAME_MAX; ++len) {
        if (name[len] == '\0')
            return len;
    }
    return SHARKIX_CAP_NAME_MAX + 1;
}

static int sharkix_bootstrap_strings_equal(const char *a, const char *b)
{
    size_t i = 0;

    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        ++i;
    }
    return a[i] == b[i];
}

static int sharkix_bootstrap_name_matches(const char *name, size_t name_len,
                                          const char *requested)
{
    size_t requested_len = sharkix_bootstrap_name_length(requested);

    if (name_len != requested_len)
        return 0;
    for (size_t i = 0; i < name_len; ++i) {
        if (name[i] != requested[i])
            return 0;
    }
    return 1;
}

static void sharkix_bootstrap_clear_handles(uint64_t *handles_out, size_t capc)
{
    for (size_t i = 0; i < capc; ++i)
        handles_out[i] = UINT64_MAX;
}

int sharkix_get_bootstrap(uint64_t *handles_out, char **capv, size_t capc,
                          uint64_t *bootstrap)
{
    uint64_t supplied_count;

    if (!bootstrap || (capc != 0 && (!handles_out || !capv)))
        return SHARKIX_BOOTSTRAP_ERR_INVALID_ARGUMENT;
    if (capc == 0)
        return SHARKIX_BOOTSTRAP_OK;

    for (size_t i = 0; i < capc; ++i) {
        size_t name_len;

        if (!capv[i])
            return SHARKIX_BOOTSTRAP_ERR_INVALID_ARGUMENT;
        name_len = sharkix_bootstrap_name_length(capv[i]);
        if (name_len == 0 || name_len > SHARKIX_CAP_NAME_MAX)
            return SHARKIX_BOOTSTRAP_ERR_INVALID_ARGUMENT;
        for (size_t j = 0; j < i; ++j) {
            if (sharkix_bootstrap_strings_equal(capv[i], capv[j]))
                return SHARKIX_BOOTSTRAP_ERR_AMBIGUOUS;
        }
    }

    supplied_count = bootstrap[0];
    if (supplied_count < capc)
        return SHARKIX_BOOTSTRAP_ERR_INVALID_DATA;

    sharkix_bootstrap_clear_handles(handles_out, capc);
    for (uint64_t supplied_index = 0;
         supplied_index < supplied_count; ++supplied_index) {
        char name[SHARKIX_CAP_NAME_MAX + 1];
        size_t name_len;
        uint64_t handle = bootstrap[supplied_index + 1];

        if (handle == 0 || handle == UINT64_MAX) {
            sharkix_bootstrap_clear_handles(handles_out, capc);
            return SHARKIX_BOOTSTRAP_ERR_INVALID_DATA;
        }
        if (sharkix_cap_get_name(handle, name, SHARKIX_CAP_NAME_MAX,
                                 &name_len) != 0) {
            sharkix_bootstrap_clear_handles(handles_out, capc);
            return SHARKIX_BOOTSTRAP_ERR_CAP_NAME;
        }
        name[name_len] = '\0';

        for (size_t requested_index = 0;
             requested_index < capc; ++requested_index) {
            if (!sharkix_bootstrap_name_matches(name, name_len,
                                                capv[requested_index]))
                continue;
            if (handles_out[requested_index] != UINT64_MAX) {
                sharkix_bootstrap_clear_handles(handles_out, capc);
                return SHARKIX_BOOTSTRAP_ERR_AMBIGUOUS;
            }
            handles_out[requested_index] = handle;
        }
    }

    for (size_t i = 0; i < capc; ++i) {
        if (handles_out[i] == UINT64_MAX) {
            sharkix_bootstrap_clear_handles(handles_out, capc);
            return SHARKIX_BOOTSTRAP_ERR_NOT_FOUND;
        }
    }
    return SHARKIX_BOOTSTRAP_OK;
}
