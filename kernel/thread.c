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
    thread->state = THREAD_READY;
    return thread;
}

static void thread_attach(thread_t *thread, TaskHandle_t task, size_t stack_size)
{
    thread->freertos_task = task;
    thread->kernel_stack_base = pxTaskGetStackBase(task);
    thread->kernel_stack_size = stack_size;
    thread->kernel_stack_top = (uintptr_t)thread->kernel_stack_base + stack_size;
    vTaskSetSharkThread(task, thread);
}

thread_t *thread_create_kernel(TaskFunction_t entry, const char *name, UBaseType_t priority)
{
    thread_t *thread = thread_allocate();
    TaskHandle_t task;
    if (!thread || xTaskCreate(entry, name, THREAD_KERNEL_STACK_WORDS, NULL, priority, &task) != pdPASS) {
        if (thread) vPortFree(thread);
        return NULL;
    }
    thread->address_space = &kernel_address_space;
    thread_attach(thread, task, THREAD_KERNEL_STACK_SIZE);
    return thread;
}

static StackType_t *user_initial_stack(thread_t *thread, uintptr_t entry)
{
    uint64_t *stack = (uint64_t *)(thread->kernel_stack_top & ~(uintptr_t)0xf);
    *--stack = 0x23;             /* SS */
    *--stack = USER_STACK_TOP;   /* RSP */
    *--stack = 0x202;            /* RFLAGS */
    *--stack = 0x2b;             /* CS */
    *--stack = entry;            /* RIP */
    for (unsigned i = 0; i < 15; ++i) *--stack = 0;
    return (StackType_t *)stack;
}

thread_t *thread_create_user(const uint8_t *image, size_t image_size, const char *name,
                             UBaseType_t priority)
{
    if (!image || !image_size) return NULL;
    thread_t *thread = thread_allocate();
    if (!thread) return NULL;
    thread->address_space = address_space_create();
    if (!thread->address_space) { vPortFree(thread); return NULL; }
    for (size_t offset = 0; offset < image_size; offset += PAGE_SIZE) {
        uint64_t page = phys_alloc_page();
        size_t count = image_size - offset; if (count > PAGE_SIZE) count = PAGE_SIZE;
        uint8_t *destination = (uint8_t *)phys_to_virt(page);
        for (size_t i = 0; i < count; ++i) destination[i] = image[offset + i];
        if (address_space_map_page(thread->address_space, USER_CODE_BASE + offset, page,
                                   PAGE_USER | ADDRESS_SPACE_MAP_OWNED) != 0) goto failed;
    }
    uint64_t stack_page = phys_alloc_page();
    if (address_space_map_page(thread->address_space, USER_STACK_BASE, stack_page,
                               PAGE_USER | PAGE_WRITABLE | PAGE_NX | ADDRESS_SPACE_MAP_OWNED) != 0) {
        phys_free_page(stack_page); goto failed;
    }
    TaskHandle_t task;
    if (xTaskCreate((TaskFunction_t)(uintptr_t)USER_CODE_BASE, name, THREAD_KERNEL_STACK_WORDS,
                    NULL, priority, &task) != pdPASS) goto failed;
    thread->user_mode = 1;
    thread_attach(thread, task, THREAD_KERNEL_STACK_SIZE);
    vTaskSetStack(task, (StackType_t *)thread->kernel_stack_base,
                  user_initial_stack(thread, USER_CODE_BASE));
    return thread;
failed:
    address_space_destroy(thread->address_space);
    vPortFree(thread);
    return NULL;
}

void thread_prepare_current(thread_t *thread)
{
    cpu0.current_thread = thread;
    if (thread) {
        cpu0.kernel_stack_top = thread->kernel_stack_top;
        vPortSetKernelStack(thread->kernel_stack_top);
    } else {
        cpu0.kernel_stack_top = 0;
    }
}

void thread_exit_current(void)
{
    thread_t *thread = thread_current();
    if (!thread) for (;;) __asm__ volatile ("cli; hlt");
    thread->state = THREAD_DEAD;
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
        if (thread->address_space != &kernel_address_space) address_space_destroy(thread->address_space);
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
