#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <sharkix/kernel/sync.h>
#include <sharkix/kernel/thread.h>
#include <sharkix/kernel/uthash.h>

typedef uint64_t ipc_handle_t;

#define IPC_INVALID_HANDLE ((ipc_handle_t)UINT64_MAX)

// this might change on other platforms, but for now we only really support x86-64
// DO NOT change this blindly, the ABI depends on it and stuff WILL break
#define IPC_MESSAGE_WORDS 5

// initial sane-ish capacity
#define IPC_QUEUE_CAPACITY 64

// This is being kept for now because we will eventually use it for stuff like caps transfer over IPC
// Originally it was going to include SEND vs CALL, but that's been scrapped for now
typedef enum ipc_message_type_t {
	IPC_MSGTYPE_SEND = 0,
} ipc_message_type_t;

typedef struct ipc_message_t {
	ipc_message_type_t type;
	uint64_t sender_tid;
	uint64_t words[5];
} ipc_message_t;

typedef enum ipc_endpoint_type_t {
	IPC_ENDPOINT_NORMAL     = 0,
	IPC_ENDPOINT_PUBLISHER  = 1,
	IPC_ENDPOINT_SUBSCRIBER = 2,
} ipc_endpoint_type_t;


typedef struct ipc_subscription_t ipc_subscription_t;

typedef struct ipc_subscription_t {
	ipc_handle_t subscriber;
	ipc_subscription_t *next;
} ipc_subscription_t;

// it is important to NOT directly mess with the contents of this struct outside of the IPC functions for multiple reasons
typedef struct ipc_endpoint_t {
	ipc_handle_t  handle;
	thread_t     *owner;

	ipc_endpoint_type_t ep_type;

	ipc_message_t queue[IPC_QUEUE_CAPACITY];
	size_t queue_head;
	size_t queue_tail;
	size_t queue_count;

	/*
	 * Protects endpoint state below and serialises queue-state decisions.
	 */
    kmutex_t lock;
	/*
	 * Used to wake blocked senders/receivers.
	 */
    ksemaphore_t sender_sem;
    ksemaphore_t receiver_sem;

	/*
	 * Endpoint lifetime.
	 *
	 * Starts at one while present in the global endpoint registry.
	 */
	size_t references;

	/*
	 * Set before removal from the registry.
	 *
	 * Existing blocked operations wake and return
	 * IPC_ERR_ENDPOINT_CLOSED.
	 */
	bool is_shutting_down;

	size_t waiting_senders;
	size_t waiting_receivers;
	

	/*
	 * This is used only by IPC_ENDPOINT_PUBLISHER
	 */
	ipc_subscription_t *subscribers;

	UT_hash_handle hh;
} ipc_endpoint_t;

typedef enum ipc_status_t {
#define SHARKIX_ERRNO(name,value,msg) name = value,
#include <sharkix/kernel/ipc_errno.inc>
#undef SHARKIX_ERRNO
} ipc_status_t;

// must be called by the kernel before userspace runs, otherwise sycalls depending on IPC will fail!
void ipc_init(void);

ipc_status_t ipc_create(ipc_handle_t *handle);
ipc_status_t ipc_destroy(ipc_handle_t handle);

// creates a new PUBSUB publisher endpoint
ipc_status_t ipc_create_publisher(ipc_handle_t *handle);

// creates a new PUBSUB subscriber endpoint, subscribed to an existing publisher
ipc_status_t ipc_subscribe(ipc_handle_t publisher, ipc_handle_t* new_subscriber);

ipc_status_t ipc_send(thread_t* caller, ipc_handle_t handle, const ipc_message_t *message);
ipc_status_t ipc_send_nb(thread_t* caller, ipc_handle_t handle, const ipc_message_t *message);

ipc_status_t ipc_recv(ipc_handle_t handle, ipc_message_t *message);
ipc_status_t ipc_recv_nb(ipc_handle_t handle, ipc_message_t *message);
