#include <sharkix/kernel/console.h>

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
        : "memory"
    );
}

static inline void pic_eoi(unsigned irq)
{
    if (irq >= 8)
        outb(0xA0, 0x20);  /* slave first */

    outb(0x20, 0x20);      /* master */
}

void kirq_handler(uint64_t irq) {
     console_write("Got IRQ: ");
     console_decimal(irq);
     console_write("\n");
     pic_eoi(irq);
}
