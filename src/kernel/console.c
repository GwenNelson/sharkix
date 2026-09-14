#include <stdint.h>
#include <stdbool.h>
#include "console.h"
#include "console-serial.h"

#include <sharkix/ddk/console-driver.h>

extern sharkix_console_driver_t __console_drivers_start[];
extern sharkix_console_driver_t __console_drivers_end[];

static bool late_drivers_ready = false;

void console_init_late(void) {
     sharkix_console_driver_t *driver;

     for (driver = __console_drivers_start; driver < __console_drivers_end; driver++) {
        if (driver->init) {
            driver->init();
        }
     }
     late_drivers_ready = true;
}

void console_putc(char c) {

     sharkix_console_driver_t *driver;
     if(late_drivers_ready) {

        for (driver = __console_drivers_start; driver < __console_drivers_end; driver++) {
            if (driver->ready) {
               if(driver->ready()) {
                    if(driver->putc) {
                        driver->putc(c);
		    }
	       }
            }
        }

     }
//    if (console_vga_isready())
//        console_vga_putc(c);
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
