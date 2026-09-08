#ifndef SHARKIX_CONSOLE_VGA_H
#define SHARKIX_CONSOLE_VGA_H

#include <stdbool.h>

void console_vga_init(void);
bool console_vga_isready(void);
void console_vga_putc(char c);

#endif
