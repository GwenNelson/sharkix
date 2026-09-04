#include "FreeRTOS.h"

#include <string.h>

#include <sharkix/kernel/ipc.h>
#include <sharkix/kernel/memory.h>

#include <libfifo/fifo.h>
#include <libfifo/sync.h>

static ipc_endpoint_t *endpoints;
static ipc_handle_t next_handle;
static fifo_mutex_t endpoints_lock;


/*
 * Look up an endpoint and take a reference to it.
 *
 * Once an endpoint has been removed from the registry by ipc_destroy(),
 * no new references can be acquired.
 */
static ipc_endpoint_t *ipc_acquire(ipc_handle_t handle) {
                       ipc_endpoint_t *endpoint;

                       fifo_mutex_lock(&endpoints_lock);

                       HASH_FIND(hh, endpoints, &handle, sizeof(handle), endpoint);

                       if (endpoint)
                           endpoint->references++;

                       fifo_mutex_unlock(&endpoints_lock);

                       return endpoint;
}


/*
 * Actually dispose of an endpoint.
 *
 * This can only happen once it has been removed from the global registry
 * and there are no operations still holding references to it.
 */
static void ipc_endpoint_free(ipc_endpoint_t *endpoint) {
            ipc_message_t *message;

            while (fifo_pop(&endpoint->queue, (void **)&message))
                kfree(message);

            kfree(endpoint);
}


/*
 * Drop an endpoint reference.
 *
 * The registry itself owns one reference for as long as the endpoint is
 * registered. ipc_destroy() removes it from the registry, wakes any blocked
 * operations, then drops that final registry reference.
 */
static void ipc_release(ipc_endpoint_t *endpoint) {
            bool free_endpoint = false;

            fifo_mutex_lock(&endpoints_lock);

            endpoint->references--;

            if (endpoint->is_shutting_down && endpoint->references == 0)
                free_endpoint = true;

            fifo_mutex_unlock(&endpoints_lock);

            if (free_endpoint)
                ipc_endpoint_free(endpoint);
}


void ipc_init(void) {
     endpoints = NULL;
     next_handle = 1;

     fifo_mutex_init(&endpoints_lock);
}


ipc_status_t ipc_create(thread_t *caller, ipc_handle_t *handle) {
             ipc_endpoint_t *endpoint;

             if (!caller || !handle)
                 return IPC_ERR_INVALID;

             endpoint = kmalloc(sizeof(*endpoint));
             if (!endpoint)
                 return IPC_ERR_NO_MEMORY;

             memset(endpoint, 0, sizeof(*endpoint));

             endpoint->owner = caller;

             fifo_init(&endpoint->queue, endpoint->queue_storage, IPC_QUEUE_CAPACITY);

             fifo_mutex_init(&endpoint->lock);
             fifo_semaphore_init(&endpoint->sender_sem, 0);
             fifo_semaphore_init(&endpoint->receiver_sem, 0);

             /*
              * The registry owns one reference.
              */
             endpoint->references = 1;
             endpoint->is_shutting_down = false;
             endpoint->waiting_senders = 0;
             endpoint->waiting_receivers = 0;

             fifo_mutex_lock(&endpoints_lock);

             endpoint->handle = next_handle++;
             HASH_ADD(hh, endpoints, handle, sizeof(endpoint->handle), endpoint);

             fifo_mutex_unlock(&endpoints_lock);

             *handle = endpoint->handle;

             return IPC_OK;
}


ipc_status_t ipc_destroy(thread_t *caller, ipc_handle_t handle) {
             ipc_endpoint_t *endpoint;
             size_t wake_senders;
             size_t wake_receivers;
             size_t i;

             if (!caller || !handle)
                 return IPC_ERR_INVALID;

             /*
              * We deliberately do this manually rather than through
              * ipc_acquire(), because removal from the registry must be
              * atomic with respect to new acquisitions.
              */
             fifo_mutex_lock(&endpoints_lock);

             HASH_FIND(hh, endpoints, &handle, sizeof(handle), endpoint);

             if (!endpoint) {
                 fifo_mutex_unlock(&endpoints_lock);
                 return IPC_ERR_NOT_FOUND;
             }

             fifo_mutex_lock(&endpoint->lock);

             if ((endpoint->owner->address_space != caller->address_space) && (endpoint->owner->privilege != caller->privilege)) {
                 fifo_mutex_unlock(&endpoint->lock);
                 fifo_mutex_unlock(&endpoints_lock);
                 return IPC_ERR_PERMISSION;
             }

             /*
              * From this point onward no operation should begin or continue.
              */
             endpoint->is_shutting_down = true;

             HASH_DEL(endpoints, endpoint);

             /*
              * These counters represent waiters which have not yet been
              * given a wakeup.
              *
              * Clear them now because destroy is satisfying every outstanding
              * wait itself.
              */
             wake_senders = endpoint->waiting_senders;
             wake_receivers = endpoint->waiting_receivers;

             endpoint->waiting_senders = 0;
             endpoint->waiting_receivers = 0;

             fifo_mutex_unlock(&endpoint->lock);
             fifo_mutex_unlock(&endpoints_lock);

             /*
              * Wake everyone blocked waiting for queue state to change.
              * They will re-enter their operation, see is_shutting_down,
              * and return IPC_ERR_ENDPOINT_CLOSED.
              */
             for (i = 0; i < wake_senders; i++)
                 fifo_semaphore_post(&endpoint->sender_sem);

             for (i = 0; i < wake_receivers; i++)
                 fifo_semaphore_post(&endpoint->receiver_sem);

             /*
              * Drop the reference which belonged to the registry.
              *
              * Active send/recv operations each hold their own reference, so
              * the endpoint cannot disappear until every woken operation has
              * returned through ipc_release().
              */
             ipc_release(endpoint);

             return IPC_OK;
}


/*
 * Blocking send.
 *
 * If the queue is full, wait until a receiver makes room.
 */
ipc_status_t ipc_send(thread_t *caller, ipc_handle_t handle, const ipc_message_t *message) {
             ipc_endpoint_t *endpoint;
             ipc_message_t *queued;

             if (!caller || !handle || !message)
                 return IPC_ERR_INVALID;

             endpoint = ipc_acquire(handle);
             if (!endpoint)
                 return IPC_ERR_NOT_FOUND;

             queued = kmalloc(sizeof(*queued));
             if (!queued) {
                 ipc_release(endpoint);
                 return IPC_ERR_NO_MEMORY;
             }

             memcpy(queued, message, sizeof(*queued));
             queued->sender_tid = caller->id;

             for (;;) {
                 fifo_mutex_lock(&endpoint->lock);

                 if (endpoint->is_shutting_down) {
                     fifo_mutex_unlock(&endpoint->lock);

                     kfree(queued);
                     ipc_release(endpoint);

                     return IPC_ERR_ENDPOINT_CLOSED;
                 }

                 if (fifo_push(&endpoint->queue, queued)) {
                     /*
                      * Wake exactly one receiver if one is waiting.
                      *
                      * Decrementing the counter here means repeated sends
                      * cannot build up stale semaphore posts for the same
                      * waiter.
                      */
                     if (endpoint->waiting_receivers) {
                         endpoint->waiting_receivers--;
                         fifo_semaphore_post(&endpoint->receiver_sem);
                     }

                     fifo_mutex_unlock(&endpoint->lock);

                     ipc_release(endpoint);
                     return IPC_OK;
                 }

                 /*
                  * Queue is full.
                  *
                  * Register ourselves as a waiter before dropping the mutex,
                  * so a receiver cannot create space between our check and
                  * our wait without noticing us.
                  */
                 endpoint->waiting_senders++;

                 fifo_mutex_unlock(&endpoint->lock);

                 fifo_semaphore_wait(&endpoint->sender_sem);

                 /*
                  * Either:
                  *
                  *   - a receiver made room;
                  *   - the endpoint is shutting down; or
                  *   - another sender got there first.
                  *
                  * Just loop and examine the real state again.
                  */
             }
}


/*
 * Non-blocking send.
 *
 * A full queue returns IPC_ERR_CANCELLED.
 */
ipc_status_t ipc_send_nb(thread_t *caller, ipc_handle_t handle, const ipc_message_t *message) {
             ipc_endpoint_t *endpoint;
             ipc_message_t *queued;

             if (!caller || !handle || !message)
                 return IPC_ERR_INVALID;

             endpoint = ipc_acquire(handle);
             if (!endpoint)
                 return IPC_ERR_NOT_FOUND;

             queued = kmalloc(sizeof(*queued));
             if (!queued) {
                 ipc_release(endpoint);
                 return IPC_ERR_NO_MEMORY;
             }

             memcpy(queued, message, sizeof(*queued));
             queued->sender_tid = caller->id;

             fifo_mutex_lock(&endpoint->lock);

             if (endpoint->is_shutting_down) {
                 fifo_mutex_unlock(&endpoint->lock);

                 kfree(queued);
                 ipc_release(endpoint);

                 return IPC_ERR_ENDPOINT_CLOSED;
             }

             if (!fifo_push(&endpoint->queue, queued)) {
                 fifo_mutex_unlock(&endpoint->lock);

                 kfree(queued);
                 ipc_release(endpoint);

                 return IPC_ERR_CANCELLED;
             }

             if (endpoint->waiting_receivers) {
                 endpoint->waiting_receivers--;
                 fifo_semaphore_post(&endpoint->receiver_sem);
             }

             fifo_mutex_unlock(&endpoint->lock);

             ipc_release(endpoint);

             return IPC_OK;
}


/*
 * Blocking receive.
 *
 * Only the owner of an endpoint may receive from it.
 */
ipc_status_t ipc_recv(thread_t *caller, ipc_handle_t handle, ipc_message_t *message) {
             ipc_endpoint_t *endpoint;
             ipc_message_t *queued;

             if (!caller || !handle || !message)
                 return IPC_ERR_INVALID;

             endpoint = ipc_acquire(handle);
             if (!endpoint)
                 return IPC_ERR_NOT_FOUND;

             for (;;) {
                 fifo_mutex_lock(&endpoint->lock);

                 if ((endpoint->owner->address_space != caller->address_space) && (endpoint->owner->privilege != caller->privilege)) {
                     fifo_mutex_unlock(&endpoint->lock);
                     ipc_release(endpoint);

                     return IPC_ERR_PERMISSION;
                 }

                 if (endpoint->is_shutting_down) {
                     fifo_mutex_unlock(&endpoint->lock);
                     ipc_release(endpoint);

                     return IPC_ERR_ENDPOINT_CLOSED;
                 }

                 if (fifo_pop(&endpoint->queue, (void **)&queued)) {
                     /*
                      * One queue slot just became available.
                      */
                     if (endpoint->waiting_senders) {
                         endpoint->waiting_senders--;
                         fifo_semaphore_post(&endpoint->sender_sem);
                     }

                     fifo_mutex_unlock(&endpoint->lock);

                     memcpy(message, queued, sizeof(*message));
                     kfree(queued);

                     ipc_release(endpoint);

                     return IPC_OK;
                 }

                 /*
                  * Nothing available. Register the wait before dropping the
                  * endpoint mutex, then sleep.
                  */
                 endpoint->waiting_receivers++;

                 fifo_mutex_unlock(&endpoint->lock);

                 fifo_semaphore_wait(&endpoint->receiver_sem);
             }
}


/*
 * Non-blocking receive.
 *
 * Empty queue returns IPC_ERR_CANCELLED.
 */
ipc_status_t ipc_recv_nb(thread_t *caller, ipc_handle_t handle, ipc_message_t *message) {
             ipc_endpoint_t *endpoint;
             ipc_message_t *queued;

             if (!caller || !handle || !message)
                 return IPC_ERR_INVALID;

             endpoint = ipc_acquire(handle);
             if (!endpoint)
                 return IPC_ERR_NOT_FOUND;

             fifo_mutex_lock(&endpoint->lock);

             if ((endpoint->owner->address_space != caller->address_space) && (endpoint->owner->privilege == caller->privilege)) {
                 fifo_mutex_unlock(&endpoint->lock);
                 ipc_release(endpoint);

                 return IPC_ERR_PERMISSION;
             }

             if (endpoint->is_shutting_down) {
                 fifo_mutex_unlock(&endpoint->lock);
                 ipc_release(endpoint);

                 return IPC_ERR_ENDPOINT_CLOSED;
             }

             if (!fifo_pop(&endpoint->queue, (void **)&queued)) {
                 fifo_mutex_unlock(&endpoint->lock);
                 ipc_release(endpoint);

                 return IPC_ERR_CANCELLED;
             }

             if (endpoint->waiting_senders) {
                 endpoint->waiting_senders--;
                 fifo_semaphore_post(&endpoint->sender_sem);
             }

             fifo_mutex_unlock(&endpoint->lock);

             memcpy(message, queued, sizeof(*message));
             kfree(queued);

             ipc_release(endpoint);

             return IPC_OK;
}


ipc_status_t ipc_call(thread_t *caller, ipc_handle_t handle, const ipc_message_t *request, ipc_message_t *reply) {
             (void)caller;
             (void)handle;
             (void)request;
             (void)reply;

             return IPC_ERR_CANCELLED;
}
