#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Sharkix synchronization interfaces.  The storage is owned by Sharkix so
 * callers never depend on a scheduler or library backend representation.
 * reserved is available to a later wait-queue backend without changing this
 * public kernel interface.
 */
typedef uintptr_t kirq_flags_t;

typedef struct kspinlock {
    atomic_flag state;
    uintptr_t reserved[2];
} kspinlock_t;

typedef struct kmutex {
    atomic_uint state;
    uintptr_t reserved[2];
} kmutex_t;

typedef struct ksemaphore {
    atomic_size_t count;
    uintptr_t reserved[2];
} ksemaphore_t;

void ksync_init(void);

void kspin_init(kspinlock_t *lock);
void kspin_lock(kspinlock_t *lock);
void kspin_unlock(kspinlock_t *lock);
kirq_flags_t kspin_lock_irqsave(kspinlock_t *lock);
void kspin_unlock_irqrestore(kspinlock_t *lock, kirq_flags_t flags);

/*
 * These are task-context waiting primitives.  The initial backend may yield
 * while retrying; callers must not use them in IRQ context or while holding a
 * kspinlock.
 */
void kmutex_init(kmutex_t *mutex);
void kmutex_lock(kmutex_t *mutex);
void kmutex_unlock(kmutex_t *mutex);

void ksem_init(ksemaphore_t *semaphore, size_t initial_count);
void ksem_wait(ksemaphore_t *semaphore);
void ksem_post(ksemaphore_t *semaphore);

/* Local CPU execution exclusion only; this is never SMP mutual exclusion. */
void kcritical_enter(void);
void kcritical_exit(void);
kirq_flags_t kirq_save(void);
void kirq_restore(kirq_flags_t flags);
