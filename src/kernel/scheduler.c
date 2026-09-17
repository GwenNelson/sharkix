#include <stddef.h>

#include "arch.h"
#include "memory.h"
#include "scheduler.h"
#include "thread.h"

static thread_t *ready_head;
static thread_t *ready_tail;
static thread_t *sleeping_threads;
static thread_t *idle_thread;
static scheduler_tick_t current_tick;
static unsigned scheduler_started;

static void idle_entry(void *argument)
{
    (void)argument;
    for (;;) arch_wait_for_interrupt();
}

static void ready_push_locked(thread_t *thread)
{
    if (!thread || thread->in_ready_queue) arch_halt();
    thread->scheduler_next = NULL;
    thread->in_ready_queue = 1;
    if (ready_tail)
        ready_tail->scheduler_next = thread;
    else
        ready_head = thread;
    ready_tail = thread;
}

static thread_t *ready_pop_locked(void)
{
    thread_t *thread = ready_head;

    if (!thread) return NULL;
    ready_head = thread->scheduler_next;
    if (!ready_head) ready_tail = NULL;
    thread->scheduler_next = NULL;
    thread->in_ready_queue = 0;
    return thread;
}

/* This is intentionally the only scheduling-policy function.  It implements
 * strict round robin: runnable threads are selected from the head of one FIFO,
 * and a yielding or preempted thread is appended at the tail.  Priorities are
 * retained in the public thread API for compatibility, but this policy does
 * not inspect them.  Replace this function when experimenting with policy. */
static thread_t *scheduler_choose_next_locked(void)
{
    thread_t *next = ready_pop_locked();
    return next ? next : idle_thread;
}

static void remove_from_sleep_list_locked(thread_t *thread)
{
    thread_t **link = &sleeping_threads;

    while (*link && *link != thread) link = &(*link)->scheduler_next;
    if (*link) {
        *link = thread->scheduler_next;
        thread->scheduler_next = NULL;
        thread->in_sleep_queue = 0;
    }
}

static void wake_expired_sleepers_locked(void)
{
    thread_t **link = &sleeping_threads;

    while (*link) {
        thread_t *thread = *link;
        if ((int64_t)(current_tick - thread->wake_tick) < 0) {
            link = &thread->scheduler_next;
            continue;
        }
        *link = thread->scheduler_next;
        thread->scheduler_next = NULL;
        thread->in_sleep_queue = 0;
        if (thread->state != THREAD_STATE_BLOCKED) arch_halt();
        thread->state = THREAD_STATE_RUNNABLE;
        ready_push_locked(thread);
    }
}

static uintptr_t switch_from_current_locked(uintptr_t saved_context)
{
    thread_t *current = thread_current();
    thread_t *next;

    if (current) {
        current->saved_context = saved_context;
        if (current->state == THREAD_STATE_RUNNING) {
            current->state = THREAD_STATE_RUNNABLE;
            if (current != idle_thread) ready_push_locked(current);
        }
    }

    next = scheduler_choose_next_locked();
    if (!next || next->state != THREAD_STATE_RUNNABLE) arch_halt();
    next->state = THREAD_STATE_RUNNING;
    thread_prepare_current(next);
    return next->saved_context;
}

int scheduler_init(void)
{
    thread_create_params_t idle_params = {
        .entry_rip = (uintptr_t)idle_entry,
        .kernel_stack_size = THREAD_DEFAULT_KERNEL_STACK_SIZE,
        .name = "idle",
        .priority = THREAD_PRIORITY_NORMAL
    };

    if (idle_thread || scheduler_started) return -1;
    ready_head = NULL;
    ready_tail = NULL;
    sleeping_threads = NULL;
    current_tick = 0;
    idle_thread = thread_create(address_space_kernel(), THREAD_PRIVILEGE_KERNEL,
                                &idle_params);
    if (!idle_thread) return -1;
    idle_thread->state = THREAD_STATE_RUNNABLE;
    return 0;
}

void scheduler_start(void)
{
    thread_t *first;

    arch_interrupts_disable();
    if (scheduler_started || !idle_thread) arch_halt();
    scheduler_started = 1;
    first = scheduler_choose_next_locked();
    if (!first || first->state != THREAD_STATE_RUNNABLE) arch_halt();
    first->state = THREAD_STATE_RUNNING;
    thread_prepare_current(first);
    arch_scheduler_start(first->saved_context);
}

int scheduler_make_runnable(thread_t *thread)
{
    uintptr_t flags = arch_irq_save();

    if (!thread || thread == idle_thread || thread->state != THREAD_STATE_READY) {
        arch_irq_restore(flags);
        return -1;
    }
    thread->state = THREAD_STATE_RUNNABLE;
    ready_push_locked(thread);
    arch_irq_restore(flags);
    return 0;
}

int scheduler_sleep_current(scheduler_tick_t ticks)
{
    uintptr_t flags;
    thread_t *thread = thread_current();

    if (!thread || thread == idle_thread || !ticks) return -1;
    flags = arch_irq_save();
    if (thread->state != THREAD_STATE_RUNNING) {
        arch_irq_restore(flags);
        return -1;
    }
    thread->state = THREAD_STATE_BLOCKED;
    thread->wake_tick = current_tick + ticks;
    thread->scheduler_next = sleeping_threads;
    thread->in_sleep_queue = 1;
    sleeping_threads = thread;
    arch_scheduler_yield();
    arch_irq_restore(flags);
    return 0;
}

int scheduler_block_current(void)
{
    uintptr_t flags;
    thread_t *thread = thread_current();

    if (!thread || thread == idle_thread) return -1;
    flags = arch_irq_save();
    if (thread->state != THREAD_STATE_RUNNING) {
        arch_irq_restore(flags);
        return -1;
    }
    thread->state = THREAD_STATE_BLOCKED;
    arch_scheduler_yield();
    arch_irq_restore(flags);
    return 0;
}

int scheduler_wake(thread_t *thread)
{
    uintptr_t flags = arch_irq_save();

    if (!thread || thread == idle_thread || thread->state != THREAD_STATE_BLOCKED) {
        arch_irq_restore(flags);
        return -1;
    }
    if (thread->in_sleep_queue) remove_from_sleep_list_locked(thread);
    thread->state = THREAD_STATE_RUNNABLE;
    ready_push_locked(thread);
    arch_irq_restore(flags);
    return 0;
}

void scheduler_exit_current(void)
{
    arch_interrupts_disable();
    arch_scheduler_yield();
    arch_halt();
}

uintptr_t scheduler_on_yield(uintptr_t saved_context)
{
    return switch_from_current_locked(saved_context);
}

uintptr_t scheduler_on_tick(uintptr_t saved_context)
{
    ++current_tick;
    wake_expired_sleepers_locked();
    return switch_from_current_locked(saved_context);
}
