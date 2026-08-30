#ifndef SHARKIX_PROGRAM_H
#define SHARKIX_PROGRAM_H

#include <stddef.h>
#include <stdint.h>
#include "thread.h"

#define PROGRAM_DEFAULT_LOAD_ADDRESS 0x0000000000400000ULL
#define PROGRAM_DEFAULT_STACK_GUARD  0x0000000000800000ULL
#define PROGRAM_DEFAULT_STACK_BASE   (PROGRAM_DEFAULT_STACK_GUARD + PAGE_SIZE)

typedef struct program_image {
    const uint8_t *data;
    size_t size;
} program_image_t;

typedef struct program_start_options {
    uintptr_t load_address;
    uintptr_t entry_address;
    uintptr_t stack_base;
    size_t stack_size;
    size_t kernel_stack_size;
    thread_privilege_t privilege;
    const char *name;
    UBaseType_t priority;
    unsigned reap_on_exit;
} program_start_options_t;

/* Low-level program construction helpers.  stack_base is immediately above an
 * intentionally unmapped user guard page selected by the caller. */
int program_map_flat_image(address_space_t *address_space, const program_image_t *image,
                           uintptr_t load_address);
int program_map_user_stack(address_space_t *address_space, uintptr_t stack_base,
                           size_t stack_size, uintptr_t *stack_top);

/* Convenience composition: create AS, map flat image and stack, create then
 * start its initial Thread.  out_address_space receives the constructor's
 * owning reference; callers release it.  Without that output, the constructor
 * reference is dropped after successful startup.  out_thread is an observing
 * pointer valid until the normal deferred reaper destroys that Thread. */
int program_load_and_start(const program_image_t *image, const program_start_options_t *options,
                           address_space_t **out_address_space, thread_t **out_thread);

#endif
