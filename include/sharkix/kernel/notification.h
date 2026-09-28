#pragma once

#include <stddef.h>
#include <stdint.h>

#include <sharkix/kernel/uthash.h>
#include <sharkix/kernel/sync.h>

typedef uint64_t notify_handle_t;

#define NOTIFY_INVALID_HANDLE ((notify_handle_t)UINT64_MAX)

typedef struct notify_t {
	notify_handle_t handle;
	size_t references;

	uint64_t pending;

	kspinlock_t  lock;
	ksemaphore_t sem;
    
	UT_hash_handle hh;
} notify_t;

void knotify_init(void);

int knotify_create(notify_handle_t *out);
int knotify_destroy(notify_handle_t handle);

// Notification-specific lifetime references for persistent kernel bindings
notify_t *knotify_acquire(notify_handle_t handle);
void knotify_release(notify_t *notify);
/* Caller must hold a reference; this operation is safe from IRQ context. */
void knotify_signal_ref(notify_t *notify, uint64_t bits);

// raise the bits specified
int knotify_signal(notify_handle_t handle, uint64_t bits);

// wait until any of the specified bits become pending
int knotify_wait(notify_handle_t handle, uint64_t bits);

// immediately returns, sets out_bits to the current state
int knotify_poll(notify_handle_t handle, uint64_t *out_bits);

// sets the specified bits as no longer pending
int knotify_ack(notify_handle_t handle, uint64_t bits);
