#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "arch.h"
#include "console.h"
#include "thread.h"

cpu_local_t cpu0;
static uint64_t next_thread_id = 1;
static thread_t *dead_threads;
static uint64_t reaped_threads;

thread_t *thread_current(void) { return cpu0.current_thread; }
uint64_t thread_current_id(void) { return cpu0.current_thread ? cpu0.current_thread->id : 0; }

static thread_t *thread_allocate(void)
{
    thread_t *thread = pvPortMalloc(sizeof(*thread));
    if (!thread) return NULL;
    uint8_t *bytes = (uint8_t *)thread;
    for (size_t i = 0; i < sizeof(*thread); ++i) bytes[i] = 0;
    thread->id = next_thread_id++;
    thread->state = THREAD_CREATED;
    return thread;
}

static StackType_t *user_initial_stack(thread_t *thread, uintptr_t entry, uintptr_t user_stack)
{
    uint64_t *stack = (uint64_t *)(thread->kernel_stack_top & ~(uintptr_t)0xf);
    *--stack = 0x23;          /* SS */
    *--stack = user_stack;    /* RSP */
    *--stack = 0x202;         /* RFLAGS */
    *--stack = 0x2b;          /* CS */
    *--stack = entry;         /* RIP */
    for (unsigned i = 0; i < 15; ++i) *--stack = 0;
    return (StackType_t *)stack;
}

static void thread_release_address_space(thread_t *thread)
{
    if (!thread->address_space) return;
    if (thread->address_space->live_threads) --thread->address_space->live_threads;
    address_space_release(thread->address_space);
    thread->address_space = NULL;
}

thread_t *thread_create(address_space_t *address_space, thread_privilege_t privilege,
                        const thread_create_params_t *params)
{
    if (!address_space || !params || !params->entry_rip) return NULL;
    if (privilege == THREAD_PRIVILEGE_USER) {
        if (address_space == address_space_kernel() || !params->initial_stack_pointer) return NULL;
    } else if (privilege != THREAD_PRIVILEGE_KERNEL) {
        return NULL;
    }

    size_t stack_size = params->kernel_stack_size ? params->kernel_stack_size : THREAD_DEFAULT_KERNEL_STACK_SIZE;
    size_t stack_words = (stack_size + sizeof(StackType_t) - 1) / sizeof(StackType_t);
    if (stack_words < 64) return NULL;

    thread_t *thread = thread_allocate();
    if (!thread) return NULL;
    thread->privilege = privilege;
    thread->address_space = address_space;
    address_space_retain(address_space);
    ++address_space->live_threads;

    TaskHandle_t task = NULL;
    BaseType_t created = xTaskCreateSuspended((TaskFunction_t)(uintptr_t)params->entry_rip,
                                               params->name ? params->name : "thread", stack_words,
                                               params->argument, params->priority, &task);
    if (created != pdPASS) {
        thread_release_address_space(thread);
        vPortFree(thread);
        return NULL;
    }

    thread->freertos_task = task;
    thread->kernel_stack_base = pxTaskGetStackBase(task);
    thread->kernel_stack_size = stack_words * sizeof(StackType_t);
    thread->kernel_stack_top = (uintptr_t)thread->kernel_stack_base + thread->kernel_stack_size;
    vTaskSetSharkThread(task, thread);
    if (privilege == THREAD_PRIVILEGE_USER)
        vTaskSetStack(task, (StackType_t *)thread->kernel_stack_base,
                      user_initial_stack(thread, params->entry_rip, params->initial_stack_pointer));
    return thread;
}

int thread_start(thread_t *thread)
{
    if (!thread || thread->state != THREAD_CREATED || !thread->freertos_task) return -1;
    thread->state = THREAD_RUNNABLE;
    vTaskStartSuspended(thread->freertos_task);
    return 0;
}

void thread_destroy_unstarted(thread_t *thread)
{
    if (!thread || thread->state != THREAD_CREATED) return;
    vTaskSetSharkThread(thread->freertos_task, NULL);
    vTaskDelete(thread->freertos_task);
    thread_release_address_space(thread);
    vPortFree(thread);
}

thread_t *thread_create_started(address_space_t *address_space, thread_privilege_t privilege,
                                const thread_create_params_t *params)
{
    thread_t *thread = thread_create(address_space, privilege, params);
    if (!thread) return NULL;
    if (thread_start(thread) != 0) {
        thread_destroy_unstarted(thread);
        return NULL;
    }
    return thread;
}

uint64_t thread_prepare_current(thread_t *thread)
{
    cpu0.current_thread = thread;
    if (!thread) {
        /* The internal FreeRTOS idle task is an explicit unmanaged kernel
         * task.  It never inherits stale Thread/TSS state. */
        cpu0.kernel_stack_top = 0;
        vPortSetKernelStack(0);
        return address_space_kernel()->pml4_phys;
    }
    thread->state = THREAD_RUNNING;
    cpu0.kernel_stack_top = thread->kernel_stack_top;
    vPortSetKernelStack(thread->kernel_stack_top);
    return thread->address_space->pml4_phys;
}

void thread_exit_current(void)
{
    thread_t *thread = thread_current();
    if (!thread) for (;;) __asm__ volatile ("cli; hlt");
    thread->state = THREAD_DYING;
    vTaskSetSharkThread(thread->freertos_task, NULL);
    thread->next_dead = dead_threads;
    dead_threads = thread;
    vTaskDelete(NULL);
    taskYIELD();
    for (;;) __asm__ volatile ("cli; hlt");
}

void thread_reap(void)
{
    while (dead_threads) {
        thread_t *thread = dead_threads;
        dead_threads = thread->next_dead;
        thread->state = THREAD_DEAD;
        thread_release_address_space(thread);
        vPortFree(thread);
        ++reaped_threads;
    }
}

uint64_t thread_reaped_count(void) { return reaped_threads; }

void thread_handle_exception(unsigned vector, uint64_t rip, uint64_t error, uint64_t address)
{
    console_write("user exception thread "); console_decimal(thread_current_id());
    console_write(" vector "); console_decimal(vector); console_write(" rip "); console_hex(rip);
    if (vector == 14) { console_write(" address "); console_hex(address); console_write(" error "); console_hex(error); }
    console_write(" cpl 3\n");
    thread_exit_current();
}
