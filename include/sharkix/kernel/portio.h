#pragma once

#include <stdint.h>

#include <sharkix/kernel/uthash.h>

typedef uint64_t portio_handle_t;

#define PORTIO_INVALID_HANDLE ((portio_handle_t)UINT64_MAX)

typedef struct portio_t {
    portio_handle_t handle;
    uint16_t        base;
    uint32_t        length;

    UT_hash_handle hh;
} portio_t;

typedef enum portio_status_t {
#define SHARKIX_ERRNO(name,value,msg) name = value,
#include <sharkix/kernel/portio_errno.inc>
#undef SHARKIX_ERRNO
} portio_status_t;

void kportio_init(void);

int kportio_create(portio_handle_t *out, uint16_t base, uint32_t length);
int kportio_get(portio_handle_t handle, portio_t *out);
int kportio_destroy(portio_handle_t handle);

int kportio_inb(portio_handle_t handle, uint32_t offset, uint8_t *value);
int kportio_inw(portio_handle_t handle, uint32_t offset, uint16_t *value);
int kportio_inl(portio_handle_t handle, uint32_t offset, uint32_t *value);

int kportio_outb(portio_handle_t handle, uint32_t offset, uint8_t value);
int kportio_outw(portio_handle_t handle, uint32_t offset, uint16_t value);
int kportio_outl(portio_handle_t handle, uint32_t offset, uint32_t value);
