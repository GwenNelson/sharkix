#pragma once

#include <stdbool.h>

/*
 * Console driver descriptor.
 *
 * Console driver linker sets are treated as contiguous arrays of these
 * structures, so registrations must use the natural alignment of this type.
 */
typedef struct sharkix_console_driver_t {
    const char *name;
    void (*init)(void);
    bool (*ready)(void);
    void (*putc)(char c);
} sharkix_console_driver_t;


/*
 * Register a normal console driver.
 *
 * This macro must be used at global scope.
 */
#define REGISTER_CONSOLE_DRIVER(driver_name, init_fn, ready_fn)       \
    static sharkix_console_driver_t                                            \
        __attribute__((section(".console_drivers")))                           \
        __attribute__((used))                                                  \
        __attribute__((aligned(__alignof__(sharkix_console_driver_t))))        \
        _console_##driver_name = {                                             \
            .name  = #driver_name,                                             \
            .init  = init_fn,                                                  \
            .ready = ready_fn,                                                 \
        }


/*
 * Register an early console driver.
 *
 * This macro must be used at global scope.
 */
#define REGISTER_EARLY_CONSOLE_DRIVER(driver_name, init_fn, ready_fn, putc_fn) \
    static sharkix_console_driver_t                                            \
        __attribute__((section(".console_early_drivers")))                     \
        __attribute__((used))                                                  \
        __attribute__((aligned(__alignof__(sharkix_console_driver_t))))        \
        _console_##driver_name = {                                             \
            .name  = #driver_name,                                             \
            .init  = init_fn,                                                  \
            .ready = ready_fn,                                                 \
            .putc  = putc_fn,                                                  \
        }
