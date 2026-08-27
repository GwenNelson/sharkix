#ifndef SHARKIX_TASK_LOADER_H
#define SHARKIX_TASK_LOADER_H

#include <stddef.h>
#include <stdint.h>
#include "memory.h"
#include "task.h"

typedef struct isolated_task {
    address_space_t *address_space;
    TaskHandle_t task;
    uint64_t code_physical;
    uint64_t stack_physical;
} isolated_task_t;

int isolated_task_create(const uint8_t *image, size_t image_size,
                         const char *name, isolated_task_t *result);

#endif
