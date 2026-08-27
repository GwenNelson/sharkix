#ifndef SHARKIX_THREAD_H
#define SHARKIX_THREAD_H

#include <stddef.h>
#include <stdint.h>
#include "memory.h"
#include "task.h"

#define THREAD_DEFAULT_KERNEL_STACK_WORDS 512U
#define THREAD_DEFAULT_KERNEL_STACK_SIZE (THREAD_DEFAULT_KERNEL_STACK_WORDS * sizeof(StackType_t))

typedef enum thread_privilege {
    THREAD_PRIVILEGE_KERNEL,
    THREAD_PRIVILEGE_USER
} thread_privilege_t;

typedef enum thread_state {
    THREAD_CREATED,
    THREAD_RUNNABLE,
    THREAD_RUNNING,
    THREAD_DYING,
    THREAD_DEAD
} thread_state_t;

typedef struct thread_create_params {
    uintptr_t entry_rip;
    uintptr_t initial_stack_pointer;
    size_t kernel_stack_size;
    const char *name;
    UBaseType_t priority;
    void *argument;
} thread_create_params_t;

typedef struct thread {
    uint64_t id;
    thread_state_t state;
    thread_privilege_t privilege;
    address_space_t *address_space;
    void *kernel_stack_base;
    uintptr_t kernel_stack_top;
    size_t kernel_stack_size;
    TaskHandle_t freertos_task;
    struct thread *next_dead;
} thread_t;

typedef struct cpu_local {
    thread_t *current_thread;
    uintptr_t kernel_stack_top;
    uintptr_t saved_user_rsp;
} cpu_local_t;

extern cpu_local_t cpu0;
thread_t *thread_current(void);
uint64_t thread_current_id(void);

/* Creation returns a suspended, non-runnable thread.  The thread retains one
 * reference to address_space until deferred reaping. */
thread_t *thread_create(address_space_t *address_space, thread_privilege_t privilege,
                        const thread_create_params_t *params);
int thread_start(thread_t *thread);
thread_t *thread_create_started(address_space_t *address_space, thread_privilege_t privilege,
                                const thread_create_params_t *params);
void thread_destroy_unstarted(thread_t *thread);

/* Called by the scheduler port for every selected FreeRTOS task.  The return
 * value is the CR3 root which the assembly port should activate. */
uint64_t thread_prepare_current(thread_t *thread);
void thread_exit_current(void) __attribute__((noreturn));
void thread_reap(void);
uint64_t thread_reaped_count(void);
void thread_handle_exception(unsigned vector, uint64_t rip, uint64_t error,
                             uint64_t address) __attribute__((noreturn));

#endif
