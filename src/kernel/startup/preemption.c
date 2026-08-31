#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "startup.h"

static volatile uint64_t worker_a_count;
static volatile uint64_t worker_b_count;
static volatile unsigned worker_a_if_enabled;
static volatile unsigned worker_b_if_enabled;
static thread_t *worker_a_thread;
static thread_t *worker_b_thread;

typedef struct preemption_worker_args {
    volatile uint64_t *counter;
    volatile unsigned *if_enabled;
    char marker;
} preemption_worker_args_t;

static preemption_worker_args_t worker_a_args = {
    .counter = &worker_a_count,
    .if_enabled = &worker_a_if_enabled,
    .marker = 'A'
};

static preemption_worker_args_t worker_b_args = {
    .counter = &worker_b_count,
    .if_enabled = &worker_b_if_enabled,
    .marker = 'B'
};

static unsigned interrupts_enabled(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    return (flags & (1ULL << 9)) != 0;
}

static void preemption_worker(void *argument)
{
    preemption_worker_args_t *args = argument;
    uint64_t local = 0;

    *args->if_enabled = interrupts_enabled();
    for (;;) {
        *args->counter = ++local;
        if ((local & 0x00ffffffULL) == 0) console_putc(args->marker);
    }
}

static void preemption_monitor(void *argument)
{
    thread_create_params_t worker_params = {
        .entry_rip = (uintptr_t)preemption_worker,
        .initial_stack_pointer = 0,
        .kernel_stack_size = 64 * PAGE_SIZE,
        .name = "preempt-a",
        .priority = tskIDLE_PRIORITY + 2,
        .argument = &worker_a_args
    };
    uint64_t last_a = 0;
    uint64_t last_b = 0;
    UBaseType_t initial_high_water_a;
    UBaseType_t initial_high_water_b;
    unsigned passes = 0;
    unsigned reported_stable = 0;
    (void)argument;

    /* The reaper also sleeps between passes, so this delay leaves only the
     * unmanaged FreeRTOS idle task runnable between PIT ticks. */
    if (thread_delay_current(2) != 0) {
        console_write("idle scheduling test failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    console_write("idle scheduling verified\n");

    worker_a_thread = thread_create_started(address_space_kernel(),
                                             THREAD_PRIVILEGE_KERNEL,
                                             &worker_params);
    worker_params.name = "preempt-b";
    worker_params.argument = &worker_b_args;
    worker_b_thread = thread_create_started(address_space_kernel(),
                                             THREAD_PRIVILEGE_KERNEL,
                                             &worker_params);
    if (!worker_a_thread || !worker_b_thread) {
        console_write("kernel preemption worker creation failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    if (address_space_translate(address_space_kernel(),
                                (uintptr_t)worker_a_thread->kernel_stack_base - PAGE_SIZE) != UINT64_MAX ||
        address_space_translate(address_space_kernel(), worker_a_thread->kernel_stack_top) != UINT64_MAX ||
        address_space_translate(address_space_kernel(),
                                (uintptr_t)worker_b_thread->kernel_stack_base - PAGE_SIZE) != UINT64_MAX ||
        address_space_translate(address_space_kernel(), worker_b_thread->kernel_stack_top) != UINT64_MAX) {
        console_write("kernel stack guard mapping failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    console_write("kernel stack guards verified\n");

    while (worker_a_count == 0 || worker_b_count == 0) {}
    if (!worker_a_if_enabled || !worker_b_if_enabled) {
        console_write("kernel preemption failed: worker IF clear\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    console_write("kernel preemption verified: non-cooperative A/B workers time-sliced\n");
    initial_high_water_a = uxTaskGetStackHighWaterMark(worker_a_thread->freertos_task);
    initial_high_water_b = uxTaskGetStackHighWaterMark(worker_b_thread->freertos_task);
    console_write("kernel stack high water words: A ");
    console_decimal(initial_high_water_a);
    console_write(" B ");
    console_decimal(initial_high_water_b);
    console_putc('\n');

    for (;;) {
        uint64_t current_a = worker_a_count;
        uint64_t current_b = worker_b_count;
        if (current_a != last_a && current_b != last_b) {
            last_a = current_a;
            last_b = current_b;
            if (++passes == 32) {
                if (!reported_stable) {
                    UBaseType_t later_a = uxTaskGetStackHighWaterMark(worker_a_thread->freertos_task);
                    UBaseType_t later_b = uxTaskGetStackHighWaterMark(worker_b_thread->freertos_task);
                    if (later_a != initial_high_water_a || later_b != initial_high_water_b) {
                        console_write("kernel stack usage did not remain bounded\n");
                        for (;;) __asm__ volatile ("cli; hlt");
                    }
                    console_write("kernel stack high water later: A ");
                    console_decimal(later_a);
                    console_write(" B ");
                    console_decimal(later_b);
                    console_putc('\n');
                    reported_stable = 1;
                } else {
                    console_putc('P');
                }
                passes = 0;
            }
        }
    }
}

void kernel_startup_profile(void)
{
    if (!startup_kernel_thread(preemption_monitor, "preempt-mon", tskIDLE_PRIORITY + 2)) {
        console_write("kernel preemption profile task creation failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
}
