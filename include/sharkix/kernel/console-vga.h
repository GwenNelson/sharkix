#pragma once

#include <stdbool.h>

void console_vga_init(void);
bool console_vga_isready(void);
void console_vga_putc(char c);
