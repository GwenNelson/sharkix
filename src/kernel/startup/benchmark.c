#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "caps.h"
#include "console.h"
#include "ipc.h"
#include "memory.h"
#include "program.h"
#include "startup.h"
#include "syscall.h"
#include "thread.h"

#define BENCH_WORKERS       100U
#define BENCH_WARMUP_ROUNDS 100U
#define BENCH_MEASURE_ROUNDS 10000U
#define BENCH_HOPS          ((uint64_t)BENCH_WORKERS * BENCH_MEASURE_ROUNDS)

extern const uint8_t ipc_benchmark_image_start[];
extern const uint8_t ipc_benchmark_image_end[];

static inline uint64_t benchmark_tsc_start(void)
{
    uint32_t lo, hi;
    __asm__ volatile("lfence\nrdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t benchmark_tsc_stop(void)
{
    uint32_t lo, hi;
    /* qemu64 does not expose RDTSCP; LFENCE/RDTSC/LFENCE is serialized. */
    __asm__ volatile("lfence\nrdtsc\nlfence" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

static void benchmark_halt(const char *message) __attribute__((noreturn));
static void benchmark_halt(const char *message)
{
    console_write("BENCHMARK FAIL: ");
    console_write(message);
    console_putc('\n');
    vTaskSuspendAll();
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

static ipc_handle_t raw_endpoints[BENCH_WORKERS];
static uint64_t raw_thread_ids[BENCH_WORKERS];
static uint64_t raw_total_cycles;
static volatile unsigned raw_ready;
static volatile unsigned raw_finished;

static void raw_worker(void *argument)
{
    unsigned worker = *(unsigned *)argument;
    unsigned round = 0;
    unsigned measuring = 0;
    ipc_message_t message;

    ++raw_ready;
    for (;;) {
        if (ipc_recv(raw_endpoints[worker], &message) != IPC_OK)
            break;

        if (worker == 0 && !measuring && round == BENCH_WARMUP_ROUNDS) {
            uint64_t start = benchmark_tsc_start();
            (void)ipc_send(thread_current(), raw_endpoints[1], &message);
            measuring = 1;
            round = 0;
            /* The end boundary is taken after the final dependent receive. */
            raw_total_cycles = start;
            continue;
        }

        if (worker == 0 && measuring) {
            ++round;
            if (round == BENCH_MEASURE_ROUNDS) {
                raw_total_cycles = benchmark_tsc_stop() - raw_total_cycles;
                ++raw_finished;
                break;
            }
        }
        (void)ipc_send(thread_current(),
                       raw_endpoints[(worker + 1U) % BENCH_WORKERS], &message);
        if (worker == 0 && !measuring)
            ++round;
    }
}

static void wait_until(volatile unsigned *value, unsigned expected)
{
    while (*value < expected)
        thread_yield();
}

static void wait_for_threads(const uint64_t *ids)
{
    unsigned i;
    for (i = 0; i < BENCH_WORKERS; ++i)
        while (thread_get_state(ids[i]) != THREAD_STATE_INVALID)
            thread_yield();
}

static void run_raw_benchmark(void)
{
    thread_create_params_t params;
    unsigned indices[BENCH_WORKERS];
    unsigned i;
    ipc_message_t token = { 0 };

    raw_ready = 0;
    raw_finished = 0;
    raw_total_cycles = 0;
    for (i = 0; i < BENCH_WORKERS; ++i) {
        if (ipc_create(&raw_endpoints[i]) != IPC_OK)
            benchmark_halt("raw endpoint create");
        indices[i] = i;
    }
    for (i = 0; i < BENCH_WORKERS; ++i) {
        params = (thread_create_params_t) {
            .entry_rip = (uintptr_t)raw_worker,
            .kernel_stack_size = 64 * PAGE_SIZE,
            .name = "raw-bench", .priority = tskIDLE_PRIORITY + 2,
            .argument = &indices[i]
        };
        thread_t *thread = thread_create_started(address_space_kernel(),
                                                  THREAD_PRIVILEGE_KERNEL,
                                                  &params);
        if (!thread)
            benchmark_halt("raw thread create");
        raw_thread_ids[i] = thread->id;
    }
    wait_until(&raw_ready, BENCH_WORKERS);
    token.type = IPC_MSGTYPE_SEND;
    if (ipc_send(thread_current(), raw_endpoints[0], &token) != IPC_OK)
        benchmark_halt("raw token injection");
    wait_until(&raw_finished, 1);

    for (i = 0; i < BENCH_WORKERS; ++i)
        (void)ipc_destroy(raw_endpoints[i]);
    wait_for_threads(raw_thread_ids);

    console_write("raw IPC:\n  hops: ");
    console_decimal(BENCH_HOPS);
    console_write("\n  total cycles: ");
    console_decimal(raw_total_cycles);
    console_write("\n  cycles/hop: ");
    console_decimal(raw_total_cycles / BENCH_HOPS);
    console_putc('\n');
}

typedef struct user_bench_task {
    address_space_t *address_space;
    thread_t *thread;
    cap_handle_t receive_cap;
    cap_handle_t send_cap;
    uint64_t *stack_slots;
} user_bench_task_t;

static int create_user_bench_task(const program_image_t *image,
                                  unsigned index, user_bench_task_t *task)
{
    uintptr_t stack_top;
    uint64_t physical;
    thread_create_params_t params;

    task->address_space = address_space_create(0);
    if (!task->address_space ||
        program_map_flat_image(task->address_space, image,
                               PROGRAM_DEFAULT_LOAD_ADDRESS) != 0 ||
        program_map_user_stack(task->address_space, PROGRAM_DEFAULT_STACK_BASE,
                               PAGE_SIZE, &stack_top) != 0)
        return -1;
    physical = address_space_translate(task->address_space,
                                       stack_top - 3 * sizeof(uint64_t));
    if (physical == UINT64_MAX)
        return -1;
    task->stack_slots = (uint64_t *)phys_to_virt(physical);
    params = (thread_create_params_t) {
        .entry_rip = PROGRAM_DEFAULT_LOAD_ADDRESS,
        .initial_stack_pointer = stack_top - 3 * sizeof(uint64_t),
        .name = "user-bench", .priority = tskIDLE_PRIORITY + 2
    };
    task->thread = thread_create(task->address_space, THREAD_PRIVILEGE_USER, &params);
    if (!task->thread)
        return -1;
    (void)index;
    return 0;
}

static void run_user_benchmark(void)
{
    const program_image_t image = {
        .data = ipc_benchmark_image_start,
        .size = (size_t)(ipc_benchmark_image_end - ipc_benchmark_image_start)
    };
    user_bench_task_t tasks[BENCH_WORKERS] = { 0 };
    ipc_handle_t endpoints[BENCH_WORKERS];
    uint64_t thread_ids[BENCH_WORKERS];
    unsigned i;
    ipc_message_t token = { .type = IPC_MSGTYPE_SEND };

    syscall_benchmark_reset();
    for (i = 0; i < BENCH_WORKERS; ++i)
        if (ipc_create(&endpoints[i]) != IPC_OK)
            benchmark_halt("user endpoint create");
    for (i = 0; i < BENCH_WORKERS; ++i) {
        if (create_user_bench_task(&image, i, &tasks[i]) != 0)
            benchmark_halt("user task create");
        if (kcap_create(endpoints[i], CAP_TYPE_IPC_ENDPOINT, CAP_RIGHT_IPC_RECV,
                        &tasks[i].receive_cap) != 0 ||
            kcapset_addcap(tasks[i].address_space->capset, tasks[i].receive_cap) != 0 ||
            kcap_create(endpoints[(i + 1U) % BENCH_WORKERS], CAP_TYPE_IPC_ENDPOINT,
                        CAP_RIGHT_IPC_SEND, &tasks[i].send_cap) != 0 ||
            kcapset_addcap(tasks[i].address_space->capset, tasks[i].send_cap) != 0)
            benchmark_halt("user capability setup");
        tasks[i].stack_slots[0] = tasks[i].receive_cap;
        tasks[i].stack_slots[1] = tasks[i].send_cap;
        tasks[i].stack_slots[2] = i;
        thread_ids[i] = tasks[i].thread->id;
    }
    for (i = 0; i < BENCH_WORKERS; ++i)
        if (thread_start(tasks[i].thread) != 0)
            benchmark_halt("user thread start");
    while (syscall_benchmark_ready_count() < BENCH_WORKERS)
        thread_yield();
    if (ipc_send(thread_current(), endpoints[0], &token) != IPC_OK)
        benchmark_halt("user token injection");
    while (!syscall_benchmark_stop_tsc())
        thread_yield();

    for (i = 0; i < BENCH_WORKERS; ++i)
        (void)ipc_destroy(endpoints[i]);
    wait_for_threads(thread_ids);
    for (i = 0; i < BENCH_WORKERS; ++i) {
        (void)kcap_destroy(tasks[i].receive_cap);
        (void)kcap_destroy(tasks[i].send_cap);
        address_space_release(tasks[i].address_space);
    }

    uint64_t total = syscall_benchmark_stop_tsc() - syscall_benchmark_start_tsc();
    console_write("userspace capability IPC:\n  hops: ");
    console_decimal(BENCH_HOPS);
    console_write("\n  total cycles: ");
    console_decimal(total);
    console_write("\n  cycles/hop: ");
    console_decimal(total / BENCH_HOPS);
    console_putc('\n');
    console_write("difference cycles/hop: ");
    console_decimal((total / BENCH_HOPS) - (raw_total_cycles / BENCH_HOPS));
    console_write("\nratio userspace/raw: ");
    console_decimal((total / BENCH_HOPS) / (raw_total_cycles / BENCH_HOPS));
    console_putc('\n');
}

void kernel_startup_profile(void)
{
    run_raw_benchmark();
    run_user_benchmark();
    startup_reaper();
}
