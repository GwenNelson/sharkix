#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "memory.h"
#include "task_loader.h"

#define TASK_CODE_BASE  0x0000000000400000ULL
#define TASK_STACK_BASE 0x0000000000800000ULL

static void copy_page(uint8_t *destination, const uint8_t *source, size_t count)
{
    for (size_t i = 0; i < count; ++i) destination[i] = source[i];
}

int isolated_task_create(const uint8_t *image, size_t image_size,
                         const char *name, isolated_task_t *result)
{
    if (!image || !image_size || !result) return -1;

    address_space_t *address_space = address_space_create();
    if (!address_space) return -1;

    uint64_t first_code_page = 0;
    for (size_t offset = 0; offset < image_size; offset += PAGE_SIZE) {
        uint64_t physical_page = phys_alloc_page();
        size_t bytes = image_size - offset;
        if (bytes > PAGE_SIZE) bytes = PAGE_SIZE;
        copy_page((uint8_t *)phys_to_virt(physical_page), image + offset, bytes);
        if (address_space_map_page(address_space, TASK_CODE_BASE + offset,
                                   physical_page, 0) != 0) return -1;
        if (!first_code_page) first_code_page = physical_page;
    }

    uint64_t stack_physical = phys_alloc_page();
    if (address_space_map_page(address_space, TASK_STACK_BASE, stack_physical,
                               PAGE_WRITABLE) != 0) return -1;

    uint8_t *stack_bytes = (uint8_t *)phys_to_virt(stack_physical);
    for (size_t i = 0; i < PAGE_SIZE; ++i) stack_bytes[i] = 0xa5;

    TaskHandle_t task;
    if (xTaskCreate((TaskFunction_t)(uintptr_t)TASK_CODE_BASE, name,
                    configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1,
                    &task) != pdPASS) return -1;

    StackType_t *stack_alias = (StackType_t *)phys_to_virt(stack_physical);
    StackType_t *initial_stack = pxPortInitialiseStack(
        stack_alias + PAGE_SIZE / sizeof(*stack_alias) - 1, stack_alias,
        (TaskFunction_t)(uintptr_t)TASK_CODE_BASE, NULL);
    uintptr_t stack_offset = (uintptr_t)initial_stack - (uintptr_t)stack_alias;

    vTaskSetAddressSpace(task, address_space);
    vTaskSetStack(task, (StackType_t *)(uintptr_t)TASK_STACK_BASE,
                  (StackType_t *)(uintptr_t)(TASK_STACK_BASE + stack_offset));

    result->address_space = address_space;
    result->task = task;
    result->code_physical = first_code_page;
    result->stack_physical = stack_physical;
    return 0;
}
