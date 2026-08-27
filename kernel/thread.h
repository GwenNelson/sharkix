#ifndef SHARKIX_THREAD_H
#define SHARKIX_THREAD_H

#include <stddef.h>
#include <stdint.h>
#include "memory.h"
#include "task.h"

#define THREAD_KERNEL_STACK_WORDS 512U
#define THREAD_KERNEL_STACK_SIZE (THREAD_KERNEL_STACK_WORDS * sizeof(StackType_t))
#define USER_CODE_BASE 0x0000000000400000ULL
#define USER_STACK_GUARD 0x0000000000800000ULL
#define USER_STACK_BASE (USER_STACK_GUARD + PAGE_SIZE)
#define USER_STACK_TOP (USER_STACK_BASE + PAGE_SIZE)

typedef enum thread_state {
    THREAD_READY,
    THREAD_DEAD
} thread_state_t;

typedef struct thread {
    uint64_t id;
    address_space_t *address_space;
    void *kernel_stack_base;
    uintptr_t kernel_stack_top;
    size_t kernel_stack_size;
    TaskHandle_t freertos_task;
    struct thread *next_dead;
    thread_state_t state;
    unsigned user_mode;
} thread_t;

typedef struct cpu_local {
    thread_t *current_thread;
    uintptr_t kernel_stack_top;
    uintptr_t saved_user_rsp;
} cpu_local_t;

extern cpu_local_t cpu0;
thread_t *thread_current(void);
uint64_t thread_current_id(void);
thread_t *thread_create_kernel(TaskFunction_t entry, const char *name, UBaseType_t priority);
thread_t *thread_create_user(const uint8_t *image, size_t image_size, const char *name,
                             UBaseType_t priority);
void thread_prepare_current(thread_t *thread);
void thread_exit_current(void) __attribute__((noreturn));
void thread_reap(void);
uint64_t thread_reaped_count(void);
void thread_handle_exception(unsigned vector, uint64_t rip, uint64_t error, uint64_t address) __attribute__((noreturn));

#endif
