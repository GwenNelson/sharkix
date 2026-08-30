#pragma once

#include <stdint.h>

#include <libfifo/fifo.h>

#include <sharkix/kernel/thread.h>
#include <sharkix/kernel/uthash.h>

typedef uint64_t ipc_handle_t;

// this might change on other platforms, but for now we only really support x86-64
// DO NOT change this blindly, the ABI depends on it and stuff WILL break
#define IPC_MESSAGE_WORDS 5

// initial sane-ish capacity
#define IPC_QUEUE_CAPACITY 64

typedef enum ipc_message_type_t {
	IPC_MSGTYPE_SEND = 0,
	IPC_MSGTYPE_CALL = 1,
} ipc_message_type_t;

typedef struct ipc_message_t {
	ipc_message_type_t type; // SEND or CALL
	uint64_t sender_tid;
	uint64_t words[5];
} ipc_message_t;

// it is important to NOT directly mess with the contents of this struct outside of the IPC functions for multiple reasons
typedef struct ipc_endpoint_t {
	ipc_handle_t  handle;
	thread_t     *owner;

	fifo_t queue;
	void *queue_storage[IPC_QUEUE_CAPACITY];

	UT_hash_handle hh;
} ipc_endpoint_t;

typedef enum ipc_status_t {
#define SHARKIX_ERRNO(name,value) name = value,
#include <sharkix/kernel/ipc_errno.inc>
#undef SHARKIX_ERRNO
} ipc_status_t;

// must be called by the kernel before userspace runs, otherwise sycalls depending on IPC will fail!
void ipc_init(void);

// these take a caller param so we can enforce rules about who can do what on IPC queues
ipc_status_t ipc_create(thread_t* caller, ipc_handle_t *handle);
ipc_status_t ipc_destroy(thread_t* caller, ipc_handle_t handle);

ipc_status_t ipc_send(thread_t* caller, ipc_handle_t handle, const ipc_message_t *message);

ipc_status_t ipc_recv(thread_t* caller, ipc_handle_t handle, ipc_message_t *message);

ipc_status_t ipc_call(thread_t* caller, ipc_handle_t handle, const ipc_message_t *request, ipc_message_t *reply);
