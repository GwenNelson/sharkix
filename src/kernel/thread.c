#include <stddef.h>
#include <stdint.h>
#include "arch.h"
#include "console.h"
#include "sync.h"
#include "thread.h"

cpu_local_t cpu0;
static uint64_t next_thread_id = 1;
static thread_t *dead_threads;
static thread_t *final_dead_threads;
static thread_t *thread_registry;
static uint64_t reaped_threads;

/* SMP: kcritical() protects these only on the local CPU.  Thread registry and
 * reaper state require real shared locks before threads can run on multiple CPUs. */

thread_t *thread_current(void) { return cpu0.current_thread; }
uint64_t thread_current_id(void) { return cpu0.current_thread ? cpu0.current_thread->id : 0; }
void thread_yield(void) { arch_scheduler_yield(); }

static thread_t *thread_allocate(void)
{
    thread_t *thread = kmalloc(sizeof(*thread));
    if (!thread) return NULL;
    uint8_t *bytes = (uint8_t *)thread;
    for (size_t i = 0; i < sizeof(*thread); ++i) bytes[i] = 0;
    kcritical_enter();
    thread->id = next_thread_id++;
    thread->state = THREAD_STATE_NEW;
    thread->registry_next = thread_registry;
    thread_registry = thread;
    kcritical_exit();
    return thread;
}

thread_t *thread_lookup(uint64_t id)
{
    thread_t *result = NULL;

    kcritical_enter();
    for (thread_t *t = thread_registry; t; t = t->registry_next) {
        if (t->id == id) {
            result = t;
            break;
        }
    }
    kcritical_exit();
    return result;
}

thread_state_t thread_get_state(uint64_t id)
{
    thread_state_t state = THREAD_STATE_INVALID;

    kcritical_enter();
    for (thread_t *thread = thread_registry; thread; thread = thread->registry_next) {
        if (thread->id == id) {
            state = thread->state;
            break;
        }
    }
    kcritical_exit();
    return state;
}

static void thread_unlink(thread_t *thread)
{
    kcritical_enter();
    thread_t **link = &thread_registry;
    while (*link && *link != thread) link = &(*link)->registry_next;
    if (*link) *link = thread->registry_next;
    kcritical_exit();
}

static int thread_transition(thread_t *thread, thread_state_t from, thread_state_t to)
{
    int result = -1;

    kcritical_enter();
    if (thread && thread->state == from) {
        thread->state = to;
        result = 0;
    }
    kcritical_exit();
    return result;
}

static void thread_release_address_space(thread_t *thread)
{
    if (!thread->address_space) return;
    kcritical_enter();
    if (thread->address_space->live_threads) --thread->address_space->live_threads;
    address_space_release(thread->address_space);
    thread->address_space = NULL;
    kcritical_exit();
}

static void thread_release_kernel_resources(thread_t *thread)
{
    if (!thread) return;
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
    void *stack_base;

    if (!address_space || !params || !params->entry_rip) return NULL;
    if (privilege == THREAD_PRIVILEGE_USER) {
        if (address_space == address_space_kernel() || !params->initial_stack_pointer) return NULL;
    } else if (privilege != THREAD_PRIVILEGE_KERNEL) {
        return NULL;
    }

    size_t stack_size = params->kernel_stack_size ? params->kernel_stack_size : THREAD_DEFAULT_KERNEL_STACK_SIZE;
    size_t stack_words = (stack_size + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);
    if (stack_words < 64) return NULL;

    thread_t *thread = thread_allocate();
    if (!thread) return NULL;
    thread->privilege = privilege;
    thread->address_space = address_space;
    kcritical_enter();
    address_space_retain(address_space);
    ++address_space->live_threads;
    kcritical_exit();

    stack_base = kernel_stack_alloc(stack_words * sizeof(uintptr_t));
    if (!stack_base) {
        thread_release_address_space(thread);
        thread_unlink(thread);
        kfree(thread);
        return NULL;
    }
    for (size_t i = 0; i < stack_words * sizeof(uintptr_t); ++i)
        ((uint8_t *)stack_base)[i] = 0xa5;
    thread->kernel_stack_base = stack_base;
    thread->kernel_stack_size = stack_words * sizeof(uintptr_t);
    thread->kernel_stack_top = (uintptr_t)stack_base + thread->kernel_stack_size;
    thread->priority = params->priority;
    thread->saved_context = arch_thread_context_init(
        thread->kernel_stack_top, (thread_entry_t)params->entry_rip,
        params->argument, privilege == THREAD_PRIVILEGE_USER,
        params->initial_stack_pointer);
    if (!thread->saved_context) arch_halt();
    kcritical_enter();
    if (thread_transition(thread, THREAD_STATE_NEW, THREAD_STATE_READY) != 0)
        arch_halt();
    kcritical_exit();
    return thread;
}

int thread_start(thread_t *thread)
{
    return scheduler_make_runnable(thread);
}

void thread_destroy_unstarted(thread_t *thread)
{
    kcritical_enter();
    if (!thread || thread->state != THREAD_STATE_READY) {
        kcritical_exit();
        return;
    }
    thread_release_kernel_resources(thread);
    thread_release_address_space(thread);
    thread_unlink(thread);
    kfree(thread);
    kcritical_exit();
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

int thread_delay_current(scheduler_tick_t ticks)
{
    return scheduler_sleep_current(ticks);
}

void thread_prepare_current(thread_t *thread)
{
    address_space_t *previous_address_space = cpu0.current_thread
        ? cpu0.current_thread->address_space : NULL;

    if (!thread || thread->state != THREAD_STATE_RUNNING) {
        console_write("thread prepare bad state");
        if (thread) {
            console_write(" "); console_decimal(thread->state);
            console_write(" tid "); console_decimal(thread->id);
        }
        console_write("\n");
        arch_halt();
    }
    cpu0.current_thread = thread;
    cpu0.kernel_stack_top = thread->kernel_stack_top;
    arch_set_kernel_stack(thread->kernel_stack_top);
    if (thread->address_space != previous_address_space)
        address_space_activate(thread->address_space);
}

size_t thread_stack_high_water_words(const thread_t *thread)
{
    const uint8_t *bytes;
    size_t unused = 0;

    if (!thread || !thread->kernel_stack_base) return 0;
    bytes = thread->kernel_stack_base;
    while (unused < thread->kernel_stack_size && bytes[unused] == 0xa5) ++unused;
    return unused / sizeof(uintptr_t);
}

void thread_exit_current(void)
{
    thread_t *thread = thread_current();
    if (!thread || thread->state != THREAD_STATE_RUNNING) arch_halt();
    /* Keep the thread non-preemptible from TERMINATING/list insertion until
     * the scheduler has switched to another runnable thread. */
    arch_interrupts_disable();
    if (thread_transition(thread, THREAD_STATE_RUNNING, THREAD_STATE_TERMINATING) != 0)
        arch_halt();
    thread->reap_next = dead_threads;
    dead_threads = thread;
    scheduler_exit_current();
}

void thread_reap(void)
{
    /* Keep DEAD objects registered for one reaper interval.  This makes
     * THREAD_STATE_DEAD observable before the ID becomes INVALID. */
    while (final_dead_threads) {
        kcritical_enter();
        thread_t *thread = final_dead_threads;
        final_dead_threads = thread->reap_next;
        thread_unlink(thread);
        kcritical_exit();
        thread_release_kernel_resources(thread);
        thread_release_address_space(thread);
        kfree(thread);
        kcritical_enter();
        ++reaped_threads;
        kcritical_exit();
    }
    while (dead_threads) {
        kcritical_enter();
        thread_t *thread = dead_threads;
        dead_threads = thread->reap_next;
        if (thread_transition(thread, THREAD_STATE_TERMINATING, THREAD_STATE_DEAD) != 0)
            arch_halt();
        thread->reap_next = final_dead_threads;
        final_dead_threads = thread;
        kcritical_exit();
    }
}

uint64_t thread_reaped_count(void)
{
    uint64_t count;
    kcritical_enter();
    count = reaped_threads;
    kcritical_exit();
    return count;
}

int thread_block_current(syscall_ctx_t *context)
{
    thread_t *thread = thread_current();
    if (!thread || thread->state != THREAD_STATE_RUNNING || !context) return -1;
    thread->blocked_syscall_ctx = context;
    if (scheduler_block_current() != 0) {
        thread->blocked_syscall_ctx = NULL;
        return -1;
    }
    thread->blocked_syscall_ctx = NULL;
    return 0;
}
int thread_wake(thread_t *thread)
{
    return scheduler_wake(thread);
}
syscall_ctx_t *thread_get_blocked_syscall_context(thread_t *thread)
{
    syscall_ctx_t *context;
    kcritical_enter();
    context = thread && thread->state == THREAD_STATE_BLOCKED ? thread->blocked_syscall_ctx : NULL;
    kcritical_exit();
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
    arch_halt();
}
