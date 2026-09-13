#include <stddef.h>
#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

#define SYS_TEST_EXIT  1

uint64_t vga_output_cap;
uint64_t vga_ready_cap;
uint64_t vga_vram_cap;
uint64_t vga_crtc_cap;

static uint64_t syscall_call(uint64_t number, uint64_t arg0, uint64_t arg1)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = number;
    regs.rdi = arg0;
    regs.rsi = arg1;
    (void)sharkix_syscall(&regs);
    return regs.rax;
}

static void test_exit(void)
{
    (void)syscall_call(SYS_TEST_EXIT, 0, 0);
    for (;;) {
        __asm__ volatile ("pause");
    }
}

void vga_consoled_main(uint64_t *bootstrap)
{
    uint64_t handles[4];
    char *capv[] = {
        "vga.output",
        "vga.ready",
        "vga.vram",
        "vga.crtc",
    };

    if (!bootstrap || bootstrap[0] != 4) {
        sharkix_debug_puts("vga-consoled: invalid bootstrap\n");
        test_exit();
    }

    if (sharkix_get_bootstrap(handles, capv, 4, bootstrap) !=
        SHARKIX_BOOTSTRAP_OK) {
        sharkix_debug_puts("vga-consoled: bootstrap discovery failed\n");
        test_exit();
    }

    vga_output_cap = handles[0];
    vga_ready_cap = handles[1];
    vga_vram_cap = handles[2];
    vga_crtc_cap = handles[3];

    sharkix_debug_puts("vga-consoled: obtained vga.output\n");
    sharkix_debug_puts("vga-consoled: obtained vga.ready\n");
    sharkix_debug_puts("vga-consoled: obtained vga.vram\n");
    sharkix_debug_puts("vga-consoled: obtained vga.crtc\n");
    test_exit();
}
