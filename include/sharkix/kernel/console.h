#ifndef SHARKIX_CONSOLE_H
#define SHARKIX_CONSOLE_H

#include <stdint.h>
void console_init(void);
void console_putc(char c);
void console_write(const char *text);
void console_hex(uint64_t value);
void console_decimal(uint64_t value);

#endif
