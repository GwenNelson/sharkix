#pragma once

#include <stdint.h>

typedef void (*thread_entry_t)(void *argument);
typedef uint8_t thread_priority_t;
typedef uint64_t scheduler_tick_t;

#define THREAD_PRIORITY_NORMAL ((thread_priority_t)2U)
#define SCHEDULER_TICKS_PER_SECOND 10U
