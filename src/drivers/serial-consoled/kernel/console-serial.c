#include <stdint.h>
#include "console-serial.h"
#include "sync.h"

#include <sharkix/ddk/console-driver.h>

static void outb(uint16_t port, uint8_t value) { __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port)); }
static uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }

static bool serial_ready = false;
void console_serial_init(void)
{
    outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 3);
    outb(0x3f9, 0); outb(0x3fb, 3); outb(0x3fa, 0xc7); outb(0x3fc, 0x0b);
    serial_ready = true;
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

bool console_serial_isready(void) {
     return serial_ready;
}


REGISTER_EARLY_CONSOLE_DRIVER(serial,console_serial_init,console_serial_isready,console_serial_putc);
