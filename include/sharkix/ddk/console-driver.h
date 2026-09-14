#pragma once

#include <stdbool.h>

// this struct needs to be defined somewhere in every console driver within a .console_drivers section
// to make it easier, use the macros below
typedef struct sharkix_console_driver_t {
      const char *name;
      void (*init)(void);
      bool (*ready)(void);
      void  (*putc)(char c);
} sharkix_console_driver_t;


// this macro goes at global level of any .c file
#define REGISTER_CONSOLE_DRIVER(driver_name, init_fn, ready_fn, putc_fn) \
	static sharkix_console_driver_t __attribute__((section(".console_drivers"))) __attribute__((used)) \
	_console_##driver_name = { \
		.name  = #driver_name, \
		.init  = init_fn, \
		.ready = ready_fn, \
		.putc  = putc_fn, \
	}

