#include <stdint.h>
#include "console-serial.h"
#include "sync.h"

static void outb(uint16_t port, uint8_t value) { __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port)); }
static uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }

void console_serial_init(void)
{
    outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 3);
    outb(0x3f9, 0); outb(0x3fb, 3); outb(0x3fa, 0xc7); outb(0x3fc, 0x0b);
}

void console_serial_putc(char c)
{
    kcritical_enter();
    while ((inb(0x3fd) & 0x20) == 0) {}
    if (c == '\n') {
        outb(0x3f8, '\r'); outb(0x3f8, '\n');
        kcritical_exit();
        return;
    }
    outb(0x3f8, (uint8_t)c);
    kcritical_exit();
}
