#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "arch.h"
#include "console.h"
#include "memory.h"
#include "startup.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_PHYS 0xb8000ULL
static volatile uint16_t *const vga = (volatile uint16_t *)(PHYSMAP_BASE + VGA_PHYS);
static uint8_t vga_x, vga_y;

static void outb(uint16_t port, uint8_t value) { __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port)); }
static uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
void console_init(void)
{
    outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 3);
    outb(0x3f9, 0); outb(0x3fb, 3); outb(0x3fa, 0xc7); outb(0x3fc, 0x0b);
}
void console_putc(char c)
{
    while ((inb(0x3fd) & 0x20) == 0) {}
    if (c == '\n') { outb(0x3f8, '\r'); outb(0x3f8, '\n'); vga_x = 0; if (++vga_y == VGA_HEIGHT) vga_y = 0; return; }
    outb(0x3f8, (uint8_t)c);
    vga[(size_t)vga_y * VGA_WIDTH + vga_x] = 0x0f00 | (uint8_t)c;
    if (++vga_x == VGA_WIDTH) { vga_x = 0; if (++vga_y == VGA_HEIGHT) vga_y = 0; }
}
void console_write(const char *text) { while (*text) console_putc(*text++); }
void console_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    console_write("0x");
    for (int i = 15; i >= 0; --i) console_putc(digits[(value >> (i * 4)) & 0xf]);
}
void console_decimal(uint64_t value)
{
    char digits[21]; unsigned n = 0;
    if (!value) { console_putc('0'); return; }
    while (value) { digits[n++] = (char)('0' + value % 10); value /= 10; }
    while (n) console_putc(digits[--n]);
}

void vApplicationMallocFailedHook(void) { for (;;) __asm__ volatile ("cli; hlt"); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) { (void)task; (void)name; for (;;) __asm__ volatile ("cli; hlt"); }

void kernel_high_entry(uint32_t magic, uint32_t info)
{
    (void)magic; (void)info;
    console_init();
    console_write("SharkKernel x86_64\n");
    console_write("kernel virtual base: 0xffffffff80000000\n");
    console_write("physmap base:        0xffff800000000000\n");
    console_write("kernel heap base:    0xffffc00000000000\n");
    memory_init();
    arch_init_cpu_local();
    arch_init_syscalls();
    if (virt_to_phys(phys_to_virt(VGA_PHYS)) == VGA_PHYS) console_write("physmap translation: ok\n");
    kernel_startup_profile();
    console_write("starting FreeRTOS...\n");
    vTaskStartScheduler();
    for (;;) __asm__ volatile ("cli; hlt");
}
