#pragma once

#include <stdint.h>

struct thread;

typedef void (*thread_entry_t)(void *argument);
typedef uint8_t thread_priority_t;
typedef uint64_t scheduler_tick_t;

#define THREAD_PRIORITY_NORMAL ((thread_priority_t)2U)
#define SCHEDULER_TICKS_PER_SECOND 10U

int scheduler_init(void);
void scheduler_start(void) __attribute__((noreturn));
int scheduler_make_runnable(struct thread *thread);
int scheduler_sleep_current(scheduler_tick_t ticks);
int scheduler_block_current(void);
int scheduler_wake(struct thread *thread);
void scheduler_exit_current(void) __attribute__((noreturn));

/* Architecture interrupt entry points.  saved_context is opaque to the
 * portable scheduler: only the architecture creates or restores it. */
uintptr_t scheduler_on_yield(uintptr_t saved_context);
uintptr_t scheduler_on_tick(uintptr_t saved_context);
