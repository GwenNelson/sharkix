#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <sharkix/kernel/thread.h>
#include <sharkix/kernel/subsystems/kas.h>

typedef uint64_t kthread_handle_t;

#define KTHREAD_INVALID_HANDLE 0

typedef struct kthread_t {
        kthread_handle_t	handle;

	thread_t*	thread;
	as_handle_t	as_handle;

	bool started; // this might seem redundant, but it's because it allows us to abstract a change to kernel core's thread.c

	UT_hash_handle hh;
} kthread_t;


void kthread_init(void);

int kthread_create(as_handle_t as, uintptr_t entry, uintptr_t stack, kthread_handle_t *out);

int kthread_start(kthread_handle_t handle);

void kthread_destroy_unstarted(kthread_handle_t handle);
