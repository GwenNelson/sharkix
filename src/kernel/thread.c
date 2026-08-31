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
static thread_t *final_dead_threads;
static thread_t *thread_registry;
static uint64_t reaped_threads;

thread_t *thread_current(void) { return cpu0.current_thread; }
uint64_t thread_current_id(void) { return cpu0.current_thread ? cpu0.current_thread->id : 0; }

static thread_t *thread_allocate(void)
{
    thread_t *thread = kmalloc(sizeof(*thread));
    if (!thread) return NULL;
    uint8_t *bytes = (uint8_t *)thread;
    for (size_t i = 0; i < sizeof(*thread); ++i) bytes[i] = 0;
    vPortEnterCritical();
    thread->id = next_thread_id++;
    thread->state = THREAD_STATE_NEW;
    thread->registry_next = thread_registry;
    thread_registry = thread;
    vPortExitCritical();
    return thread;
}

thread_t *thread_lookup(uint64_t id)
{
    thread_t *result = NULL;

    vPortEnterCritical();
    for (thread_t *t = thread_registry; t; t = t->registry_next) {
        if (t->id == id) {
            result = t;
            break;
        }
    }
    vPortExitCritical();
    return result;
}

thread_state_t thread_get_state(uint64_t id)
{
    thread_state_t state = THREAD_STATE_INVALID;

    vPortEnterCritical();
    for (thread_t *thread = thread_registry; thread; thread = thread->registry_next) {
        if (thread->id == id) {
            state = thread->state;
            break;
        }
    }
    vPortExitCritical();
    return state;
}

static void thread_unlink(thread_t *thread)
{
    vPortEnterCritical();
    thread_t **link = &thread_registry;
    while (*link && *link != thread) link = &(*link)->registry_next;
    if (*link) *link = thread->registry_next;
    vPortExitCritical();
}

static int thread_transition(thread_t *thread, thread_state_t from, thread_state_t to)
{
    int result = -1;

    vPortEnterCritical();
    if (thread && thread->state == from) {
        thread->state = to;
        result = 0;
    }
    vPortExitCritical();
    return result;
}

void thread_scheduler_task_ready(void *association)
{
    thread_t *thread = association;

    vPortEnterCritical();
    if (thread && thread->state == THREAD_STATE_BLOCKED &&
        thread_transition(thread, THREAD_STATE_BLOCKED, THREAD_STATE_RUNNABLE) != 0)
        for (;;) __asm__ volatile ("cli; hlt");
    vPortExitCritical();
}

static StackType_t *user_initial_stack(thread_t *thread, uintptr_t entry, uintptr_t user_stack)
{
    uint64_t *stack = (uint64_t *)(thread->kernel_stack_top & ~(uintptr_t)0xf);
    *--stack = 0x2b;          /* SS */
    *--stack = user_stack;    /* RSP */
    *--stack = 0x202;         /* RFLAGS */
    *--stack = 0x33;          /* CS */
    *--stack = entry;         /* RIP */
    for (unsigned i = 0; i < 15; ++i) *--stack = 0;
    return (StackType_t *)stack;
}

static void thread_release_address_space(thread_t *thread)
{
    if (!thread->address_space) return;
    vPortEnterCritical();
    if (thread->address_space->live_threads) --thread->address_space->live_threads;
    address_space_release(thread->address_space);
    thread->address_space = NULL;
    vPortExitCritical();
}

static void thread_release_kernel_resources(thread_t *thread)
{
    if (!thread) return;
    if (thread->freertos_task) {
        kfree(thread->freertos_task);
        thread->freertos_task = NULL;
    }
    if (thread->kernel_stack_base) {
        kernel_stack_free(thread->kernel_stack_base, thread->kernel_stack_size);
        thread->kernel_stack_base = NULL;
        thread->kernel_stack_top = 0;
        thread->kernel_stack_size = 0;
    }
}

thread_t *thread_create(address_space_t *address_space, thread_privilege_t privilege,
                        const thread_create_params_t *params)
{
    StaticTask_t *task_buffer;
    void *stack_base;
    TaskHandle_t task;

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
    vPortEnterCritical();
    address_space_retain(address_space);
    ++address_space->live_threads;
    vPortExitCritical();

    stack_base = kernel_stack_alloc(stack_words * sizeof(StackType_t));
    if (!stack_base) {
        thread_release_address_space(thread);
        thread_unlink(thread);
        kfree(thread);
        return NULL;
    }
    task_buffer = kmalloc(sizeof(*task_buffer));
    if (!task_buffer) {
        kernel_stack_free(stack_base, stack_words * sizeof(StackType_t));
        thread_release_address_space(thread);
        thread_unlink(thread);
        kfree(thread);
        return NULL;
    }
    /* Do not let the tick handler observe a task between FreeRTOS creation
     * and installation of its SharkKernel association/initial frame. */
    vPortEnterCritical();
    task = xTaskCreateStaticSuspended((TaskFunction_t)(uintptr_t)params->entry_rip,
                                      params->name ? params->name : "thread", stack_words,
                                      params->argument, params->priority,
                                      (StackType_t *)stack_base, task_buffer);
    if (!task) {
        vPortExitCritical();
        kfree(task_buffer);
        kernel_stack_free(stack_base, stack_words * sizeof(StackType_t));
        thread_release_address_space(thread);
        thread_unlink(thread);
        kfree(thread);
        return NULL;
    }

    thread->freertos_task = task;
    thread->kernel_stack_base = stack_base;
    thread->kernel_stack_size = stack_words * sizeof(StackType_t);
    thread->kernel_stack_top = (uintptr_t)stack_base + thread->kernel_stack_size;
    vTaskSetSharkThread(task, thread);
    if (privilege == THREAD_PRIVILEGE_USER) {
        vTaskSetStack(task, (StackType_t *)thread->kernel_stack_base,
                      user_initial_stack(thread, params->entry_rip, params->initial_stack_pointer));
    }
    if (thread_transition(thread, THREAD_STATE_NEW, THREAD_STATE_READY) != 0)
        for (;;) __asm__ volatile ("cli; hlt");
    vPortExitCritical();
    return thread;
}

int thread_start(thread_t *thread)
{
    uint32_t interrupt_mask = ulPortSetInterruptMask();

    if (!thread || thread->state != THREAD_STATE_READY || !thread->freertos_task ||
        thread_transition(thread, THREAD_STATE_READY, THREAD_STATE_RUNNABLE) != 0) {
        vPortClearInterruptMask(interrupt_mask);
        return -1;
    }
    vTaskStartSuspended(thread->freertos_task);
    vPortClearInterruptMask(interrupt_mask);
    return 0;
}

void thread_destroy_unstarted(thread_t *thread)
{
    vPortEnterCritical();
    if (!thread || thread->state != THREAD_STATE_READY) {
        vPortExitCritical();
        return;
    }
    vTaskSetSharkThread(thread->freertos_task, NULL);
    vTaskDelete(thread->freertos_task);
    thread_release_kernel_resources(thread);
    thread_release_address_space(thread);
    thread_unlink(thread);
    kfree(thread);
    vPortExitCritical();
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

int thread_delay_current(TickType_t ticks)
{
    thread_t *thread = thread_current();
    uint32_t interrupt_mask;

    if (!thread || ticks == 0) return -1;
    interrupt_mask = ulPortSetInterruptMask();
    if (thread_transition(thread, THREAD_STATE_RUNNING, THREAD_STATE_BLOCKED) != 0) {
        vPortClearInterruptMask(interrupt_mask);
        return -1;
    }
    vTaskDelay(ticks);
    if (thread->state != THREAD_STATE_RUNNING)
        for (;;) __asm__ volatile ("cli; hlt");
    vPortClearInterruptMask(interrupt_mask);
    return 0;
}

uint64_t thread_prepare_current(thread_t *thread)
{
    if (cpu0.current_thread && cpu0.current_thread != thread &&
        cpu0.current_thread->state == THREAD_STATE_RUNNING)
        (void)thread_transition(cpu0.current_thread, THREAD_STATE_RUNNING, THREAD_STATE_RUNNABLE);
    cpu0.current_thread = thread;
    if (!thread) {
        /* The internal FreeRTOS idle task is an explicit unmanaged kernel
         * task.  It never inherits stale Thread/TSS state. */
        cpu0.kernel_stack_top = 0;
        vPortSetKernelStack(0);
        return address_space_kernel()->pml4_phys;
    }
    if (thread->state != THREAD_STATE_RUNNABLE && thread->state != THREAD_STATE_RUNNING) {
        console_write("thread prepare bad state "); console_decimal(thread->state);
        console_write(" tid "); console_decimal(thread->id); console_write("\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    if (thread->state == THREAD_STATE_RUNNABLE)
        (void)thread_transition(thread, THREAD_STATE_RUNNABLE, THREAD_STATE_RUNNING);
    cpu0.kernel_stack_top = thread->kernel_stack_top;
    vPortSetKernelStack(thread->kernel_stack_top);
    return thread->address_space->pml4_phys;
}

void thread_exit_current(void)
{
    thread_t *thread = thread_current();
    if (!thread) for (;;) __asm__ volatile ("cli; hlt");
    if (thread->state != THREAD_STATE_RUNNING) for (;;) __asm__ volatile ("cli; hlt");
    /* Keep the task non-preemptible from TERMINATING/list insertion until
     * vTaskDelete removes it from the FreeRTOS ready list and yields away. */
    portDISABLE_INTERRUPTS();
    if (thread_transition(thread, THREAD_STATE_RUNNING, THREAD_STATE_TERMINATING) != 0)
        for (;;) __asm__ volatile ("cli; hlt");
    vTaskSetSharkThread(thread->freertos_task, NULL);
    thread->reap_next = dead_threads;
    dead_threads = thread;
    vTaskDelete(NULL);
    for (;;) __asm__ volatile ("cli; hlt");
}

void thread_reap(void)
{
    /* Keep DEAD objects registered for one reaper interval.  This makes
     * THREAD_STATE_DEAD observable before the ID becomes INVALID. */
    while (final_dead_threads) {
        vPortEnterCritical();
        thread_t *thread = final_dead_threads;
        final_dead_threads = thread->reap_next;
        thread_unlink(thread);
        vPortExitCritical();
        thread_release_kernel_resources(thread);
        thread_release_address_space(thread);
        kfree(thread);
        vPortEnterCritical();
        ++reaped_threads;
        vPortExitCritical();
    }
    while (dead_threads) {
        vPortEnterCritical();
        thread_t *thread = dead_threads;
        dead_threads = thread->reap_next;
        if (thread_transition(thread, THREAD_STATE_TERMINATING, THREAD_STATE_DEAD) != 0)
            for (;;) __asm__ volatile ("cli; hlt");
        thread->reap_next = final_dead_threads;
        final_dead_threads = thread;
        vPortExitCritical();
    }
}

uint64_t thread_reaped_count(void)
{
    uint64_t count;
    vPortEnterCritical();
    count = reaped_threads;
    vPortExitCritical();
    return count;
}

int thread_block_current(syscall_ctx_t *context)
{
    thread_t *thread = thread_current();
    if (!thread || thread->state != THREAD_STATE_RUNNING || !context) return -1;
    portDISABLE_INTERRUPTS();
    thread->blocked_syscall_ctx = context;
    if (thread_transition(thread, THREAD_STATE_RUNNING, THREAD_STATE_BLOCKED) != 0) return -1;
    /* Keep the FreeRTOS task and its saved syscall frame alive while the
     * generic SharkKernel wait is outstanding.  Deleting the task here would
     * make wake impossible and would also hand ownership of its TCB to the
     * idle-task reaper. */
    vTaskSuspendCurrentNoYield();
    taskYIELD();
    thread->blocked_syscall_ctx = NULL;
    return 0;
}
int thread_wake(thread_t *thread)
{
    uint32_t interrupt_mask = ulPortSetInterruptMask();

    if (!thread || thread->state != THREAD_STATE_BLOCKED || !thread->freertos_task ||
        thread_transition(thread, THREAD_STATE_BLOCKED, THREAD_STATE_RUNNABLE) != 0) {
        vPortClearInterruptMask(interrupt_mask);
        return -1;
    }
    vTaskResume(thread->freertos_task);
    vPortClearInterruptMask(interrupt_mask);
    return 0;
}
syscall_ctx_t *thread_get_blocked_syscall_context(thread_t *thread)
{
    syscall_ctx_t *context;
    vPortEnterCritical();
    context = thread && thread->state == THREAD_STATE_BLOCKED ? thread->blocked_syscall_ctx : NULL;
    vPortExitCritical();
    return context;
}

void thread_handle_exception(unsigned vector, uint64_t rip, uint64_t error, uint64_t address)
{
    console_write("user exception thread "); console_decimal(thread_current_id());
    console_write(" vector "); console_decimal(vector); console_write(" rip "); console_hex(rip);
    if (vector == 14) { console_write(" address "); console_hex(address); console_write(" error "); console_hex(error); }
    console_write(" cpl 3\n");
    thread_exit_current();
}

void thread_handle_kernel_exception(unsigned vector, uint64_t rip, uint64_t error, uint64_t address)
{
    console_write("kernel exception vector "); console_decimal(vector);
    console_write(" rip "); console_hex(rip);
    if (vector == 14) {
        console_write(" address "); console_hex(address);
        console_write(" error "); console_hex(error);
    }
    console_write("\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
