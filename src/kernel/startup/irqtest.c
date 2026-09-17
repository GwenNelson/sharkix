#include "startup.h"
#include "thread.h"

#include <sharkix/kernel/irq.h>
#include <sharkix/kernel/portio.h>

#define KBD_IRQ  0x01
#define KBD_PORT 0x60

irq_handle_t    keyboard_irq  = IRQ_INVALID_HANDLE;
portio_handle_t keyboard_port = PORTIO_INVALID_HANDLE;

static const char scancode_ascii[128] = {
    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',
    [0x0C] = '-',
    [0x0D] = '=',
    [0x0E] = '\b',
    [0x0F] = '\t',

    [0x10] = 'q',
    [0x11] = 'w',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'y',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',
    [0x1A] = '[',
    [0x1B] = ']',
    [0x1C] = '\n',

    [0x1E] = 'a',
    [0x1F] = 's',
    [0x20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',
    [0x27] = ';',
    [0x28] = '\'',
    [0x29] = '`',

    [0x2B] = '\\',
    [0x2C] = 'z',
    [0x2D] = 'x',
    [0x2E] = 'c',
    [0x2F] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = 'm',
    [0x33] = ',',
    [0x34] = '.',
    [0x35] = '/',
    [0x39] = ' ',
};

static void irqtest_task(void *argument) { (void)argument;
	thread_yield();
	thread_yield();
	console_write("Press keys on the keyboard!\n");
	for(;;) {
            kirq_wait(keyboard_irq);
            uint8_t scancode;
	    kportio_inb(keyboard_port, 0, &scancode);
            if(!(scancode & 0x80)) {
              char c = scancode_ascii[scancode];
	      if(c)
		   console_putc(c);
	    }
	    kirq_ack(keyboard_irq);
	}

}
void kernel_startup_profile(void) {
     kirq_create(&keyboard_irq,KBD_IRQ);
     kportio_create(&keyboard_port,KBD_PORT,1);
     startup_kernel_thread(irqtest_task, "irqtest", THREAD_PRIORITY_NORMAL);
}
