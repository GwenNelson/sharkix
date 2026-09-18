#include <stdbool.h>
#include <stdint.h>

#include "console.h"

#include <sharkix/kernel/ipc.h>
#include <sharkix/kernel/ipc_registry.h>

void ps2bus_init(void);
bool ps2bus_isready(void);

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

void kernel_startup_profile(void)
{
    ipc_handle_t port1_endpoint;

    ps2bus_init();
    while (!ps2bus_isready()) {
    }

    if (kipc_registry_lookup("ps2.port1", &port1_endpoint) != 0) {
        console_write("ps2bus_test: ps2.port1 lookup failed\n");
        for (;;) {
        }
    }

    console_write("ps2bus_test: press keys\n");
    for (;;) {
        ipc_message_t message;
        uint8_t scancode;

        if (ipc_recv(port1_endpoint, &message) != IPC_OK) {
            console_write("ps2bus_test: receive failed\n");
            continue;
        }

        scancode = (uint8_t)message.words[0];
        if (!(scancode & 0x80) && scancode_ascii[scancode]) {
            console_putc(scancode_ascii[scancode]);
        }
    }
}
