#include "FreeRTOS.h"

#include <string.h>

#include <sharkix/kernel/ipc.h>
#include <sharkix/kernel/memory.h>
#include <libfifo/fifo.h>
#include <libfifo/sync.h>

static ipc_endpoint_t *endpoints;
static ipc_handle_t next_handle;

static ipc_endpoint_t *ipc_lookup(ipc_handle_t handle) {
                      ipc_endpoint_t *endpoint;

                      HASH_FIND(hh, endpoints, &handle, sizeof(handle), endpoint);

                      return endpoint;
}

void ipc_init(void) {
     vPortEnterCritical();
     endpoints = NULL;
     next_handle = 1;
     vPortExitCritical();
}

ipc_status_t ipc_create(thread_t *caller, ipc_handle_t *handle) {
             ipc_endpoint_t *endpoint;
             ipc_status_t status = IPC_OK;

             if (!caller || !handle)
                 return IPC_ERR_INVALID;

             vPortEnterCritical();
             endpoint = kmalloc(sizeof(*endpoint));
             if (!endpoint) {
                 status = IPC_ERR_NO_MEMORY;
                 goto out;
             }

             memset(endpoint, 0, sizeof(*endpoint));

             endpoint->handle = next_handle++;
             endpoint->owner = caller;

             fifo_init(&endpoint->queue, endpoint->queue_storage, IPC_QUEUE_CAPACITY);

             HASH_ADD(hh, endpoints, handle, sizeof(endpoint->handle), endpoint);

             *handle = endpoint->handle;

out:
             vPortExitCritical();
             return status;
}

ipc_status_t ipc_destroy(thread_t *caller, ipc_handle_t handle) {
             ipc_endpoint_t *endpoint;
             ipc_message_t *message;
             ipc_status_t status = IPC_OK;

             if (!caller || !handle)
                 return IPC_ERR_INVALID;

             vPortEnterCritical();
             endpoint = ipc_lookup(handle);
             if (!endpoint) {
                 status = IPC_ERR_NOT_FOUND;
                 goto out;
             }

             if (endpoint->owner != caller) {
                 status = IPC_ERR_PERMISSION;
                 goto out;
             }

             HASH_DEL(endpoints, endpoint);

             while (fifo_pop(&endpoint->queue, (void **)&message))
                 kfree(message);

             kfree(endpoint);

out:
             vPortExitCritical();
             return status;
}

ipc_status_t ipc_send(thread_t *caller, ipc_handle_t handle, const ipc_message_t *message) {
             ipc_endpoint_t *endpoint;
             ipc_message_t *queued;
             ipc_status_t status = IPC_OK;

             if (!caller || !handle || !message)
                 return IPC_ERR_INVALID;

             vPortEnterCritical();
             endpoint = ipc_lookup(handle);
             if (!endpoint) {
                 status = IPC_ERR_NOT_FOUND;
                 goto out;
             }

             queued = kmalloc(sizeof(*queued));
             if (!queued) {
                 status = IPC_ERR_NO_MEMORY;
                 goto out;
             }

             memcpy(queued, message, sizeof(*queued));
             queued->sender_tid = caller->id;

             if (!fifo_push(&endpoint->queue, queued)) {
                 kfree(queued);
                 status = IPC_ERR_CANCELLED;
             }

out:
             vPortExitCritical();
             return status;
}

ipc_status_t ipc_recv(thread_t *caller, ipc_handle_t handle, ipc_message_t *message) {
             ipc_endpoint_t *endpoint;
             ipc_message_t *queued;
             ipc_status_t status = IPC_OK;

             if (!caller || !handle || !message)
                 return IPC_ERR_INVALID;

             vPortEnterCritical();
             endpoint = ipc_lookup(handle);
             if (!endpoint) {
                 status = IPC_ERR_NOT_FOUND;
                 goto out;
             }

             if (endpoint->owner != caller) {
                 status = IPC_ERR_PERMISSION;
                 goto out;
             }

             if (!fifo_pop(&endpoint->queue, (void **)&queued)) {
                 status = IPC_ERR_CANCELLED;
                 goto out;
             }

             memcpy(message, queued, sizeof(*message));
             kfree(queued);

out:
             vPortExitCritical();
             return status;
}

ipc_status_t ipc_call(thread_t *caller, ipc_handle_t handle, const ipc_message_t *request, ipc_message_t *reply) {
             (void)caller;
             (void)handle;
             (void)request;
             (void)reply;

             return IPC_ERR_CANCELLED;
}
