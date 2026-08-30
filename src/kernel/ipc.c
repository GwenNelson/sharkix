#include "FreeRTOS.h"

#include <string.h>

#include <sharkix/kernel/ipc.h>
#include <sharkix/kernel/memory.h>

static ipc_endpoint_t *endpoints;
static ipc_handle_t next_handle;

static ipc_endpoint_t *ipc_lookup(ipc_handle_t handle) {
                      ipc_endpoint_t *endpoint;

                      HASH_FIND(hh, endpoints, &handle, sizeof(handle), endpoint);

                      return endpoint;
}

void ipc_init(void) {
     endpoints = NULL;
     next_handle = 1;
}

ipc_status_t ipc_create(thread_t *caller, ipc_handle_t *handle) {
             ipc_endpoint_t *endpoint;

             if (!caller || !handle)
                 return IPC_ERR_INVALID;

             endpoint = kmalloc(sizeof(*endpoint));
             if (!endpoint)
                 return IPC_ERR_NO_MEMORY;

             memset(endpoint, 0, sizeof(*endpoint));

             endpoint->handle = next_handle++;
             endpoint->owner = caller;

             fifo_init(&endpoint->queue, endpoint->queue_storage, IPC_QUEUE_CAPACITY);

             HASH_ADD(hh, endpoints, handle, sizeof(endpoint->handle), endpoint);

             *handle = endpoint->handle;

             return IPC_OK;
}

ipc_status_t ipc_destroy(thread_t *caller, ipc_handle_t handle) {
             ipc_endpoint_t *endpoint;
             ipc_message_t *message;

             if (!caller || !handle)
                 return IPC_ERR_INVALID;

             endpoint = ipc_lookup(handle);
             if (!endpoint)
                 return IPC_ERR_NOT_FOUND;

             if (endpoint->owner != caller)
                 return IPC_ERR_PERMISSION;

             HASH_DEL(endpoints, endpoint);

             while (fifo_pop(&endpoint->queue, (void **)&message))
                 kfree(message);

             kfree(endpoint);

             return IPC_OK;
}

ipc_status_t ipc_send(thread_t *caller, ipc_handle_t handle, const ipc_message_t *message) {
             ipc_endpoint_t *endpoint;
             ipc_message_t *queued;

             if (!caller || !handle || !message)
                 return IPC_ERR_INVALID;

             endpoint = ipc_lookup(handle);
             if (!endpoint)
                 return IPC_ERR_NOT_FOUND;

             queued = kmalloc(sizeof(*queued));
             if (!queued)
                 return IPC_ERR_NO_MEMORY;

             memcpy(queued, message, sizeof(*queued));
             queued->sender_tid = caller->id;

             if (!fifo_push(&endpoint->queue, queued)) {
                 kfree(queued);
                 return IPC_ERR_CANCELLED;
             }

             return IPC_OK;
}

ipc_status_t ipc_recv(thread_t *caller, ipc_handle_t handle, ipc_message_t *message) {
             ipc_endpoint_t *endpoint;
             ipc_message_t *queued;

             if (!caller || !handle || !message)
                 return IPC_ERR_INVALID;

             endpoint = ipc_lookup(handle);
             if (!endpoint)
                 return IPC_ERR_NOT_FOUND;

             if (endpoint->owner != caller)
                 return IPC_ERR_PERMISSION;

             if (!fifo_pop(&endpoint->queue, (void **)&queued))
                 return IPC_ERR_CANCELLED;

             memcpy(message, queued, sizeof(*message));
             kfree(queued);

             return IPC_OK;
}

ipc_status_t ipc_call(thread_t *caller, ipc_handle_t handle, const ipc_message_t *request, ipc_message_t *reply) {
             (void)caller;
             (void)handle;
             (void)request;
             (void)reply;

             return IPC_ERR_CANCELLED;
}
