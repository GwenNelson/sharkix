#pragma once

#include <stdint.h>

#include <sharkix/kernel/uthash.h>
#include <sharkix/kernel/sync.h>


typedef enum irq_status_t {
#define SHARKIX_ERRNO(name,value,msg) name = value,
#include <sharkix/kernel/irq_errno.inc>
#undef SHARKIX_ERRNO
} irq_status_t;


typedef uint64_t irq_handle_t;

#define IRQ_INVALID_HANDLE ((irq_handle_t)UINT64_MAX)
#define IRQ_COUNT          16

typedef struct irq_t {
    irq_handle_t handle;
    uint32_t     hwirq;

    /*
     * Each IRQ object gets its own semaphore. An interrupt delivery posts
     * every object attached to that hardware IRQ; kirq_wait() consumes one.
     */
    ksemaphore_t sem;

    /* Global IRQ object table, keyed by handle. */
    UT_hash_handle hh;

    /* Per-hardware-IRQ listener list. */
    struct irq_t *irq_next;
} irq_t;

void kirq_init(void);

int kirq_create(irq_handle_t *out, uint32_t hwirq);
int kirq_get(irq_handle_t handle, irq_t *out);
int kirq_destroy(irq_handle_t handle);

int kirq_wait(irq_handle_t handle);
int kirq_ack(irq_handle_t handle);

/*
 * Called by the architecture interrupt dispatcher when a hardware IRQ fires.
 */
void kirq_handle(uint64_t irq);
