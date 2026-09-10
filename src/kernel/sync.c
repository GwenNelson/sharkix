#include <sharkix/kernel/sync.h>
#include <sharkix/kernel/thread.h>

#include "FreeRTOS.h"

#include <libfifo/sync.h>

static void ksync_yield(void)
{
    thread_yield();
}

void ksync_init(void)
{
    fifo_set_yield_callback(ksync_yield);
}

void kspin_init(kspinlock_t *lock)
{
    atomic_flag_clear(&lock->state);
}

void kspin_lock(kspinlock_t *lock)
{
    while (atomic_flag_test_and_set_explicit(&lock->state, memory_order_acquire)) {
#if defined(__i386__) || defined(__x86_64__)
        __asm__ volatile ("pause");
#endif
    }
}

void kspin_unlock(kspinlock_t *lock)
{
    atomic_flag_clear_explicit(&lock->state, memory_order_release);
}

kirq_flags_t kirq_save(void)
{
    return (kirq_flags_t)ulPortSetInterruptMask();
}

void kirq_restore(kirq_flags_t flags)
{
    vPortClearInterruptMask((uint32_t)flags);
}

kirq_flags_t kspin_lock_irqsave(kspinlock_t *lock)
{
    kirq_flags_t flags = kirq_save();
    kspin_lock(lock);
    return flags;
}

void kspin_unlock_irqrestore(kspinlock_t *lock, kirq_flags_t flags)
{
    kspin_unlock(lock);
    kirq_restore(flags);
}

void kmutex_init(kmutex_t *mutex)
{
    atomic_store_explicit(&mutex->state, 0, memory_order_relaxed);
}

void kmutex_lock(kmutex_t *mutex)
{
    unsigned int expected;

    for (;;) {
        expected = 0;
        if (atomic_compare_exchange_strong_explicit(&mutex->state, &expected, 1,
                                                    memory_order_acquire,
                                                    memory_order_relaxed))
            return;
        fifo_platform_yield();
    }
}

void kmutex_unlock(kmutex_t *mutex)
{
    atomic_store_explicit(&mutex->state, 0, memory_order_release);
}

void ksem_init(ksemaphore_t *semaphore, size_t initial_count)
{
    atomic_store_explicit(&semaphore->count, initial_count, memory_order_relaxed);
}

void ksem_wait(ksemaphore_t *semaphore)
{
    size_t count;

    for (;;) {
        count = atomic_load_explicit(&semaphore->count, memory_order_relaxed);
        while (count != 0) {
            if (atomic_compare_exchange_weak_explicit(&semaphore->count, &count,
                                                      count - 1, memory_order_acquire,
                                                      memory_order_relaxed))
                return;
        }
        fifo_platform_yield();
    }
}

void ksem_post(ksemaphore_t *semaphore)
{
    atomic_fetch_add_explicit(&semaphore->count, 1, memory_order_release);
}

void kcritical_enter(void)
{
    vPortEnterCritical();
}

void kcritical_exit(void)
{
    vPortExitCritical();
}
