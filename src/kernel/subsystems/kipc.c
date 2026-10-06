#include <string.h>

#include <sharkix/kernel/subsystems/kipc.h>
#include <sharkix/kernel/memory.h>

static ipc_endpoint_t *endpoints;
static ipc_handle_t next_handle;
static kmutex_t endpoints_lock;


static bool kipc_queue_push(ipc_endpoint_t *endpoint,
                           const ipc_message_t *message) {
             if (endpoint->queue_count == IPC_QUEUE_CAPACITY)
                 return false;

             endpoint->queue[endpoint->queue_tail] = *message;
             endpoint->queue_tail = (endpoint->queue_tail + 1U) % IPC_QUEUE_CAPACITY;
             endpoint->queue_count++;
             return true;
}


static bool kipc_queue_pop(ipc_endpoint_t *endpoint,
                          ipc_message_t *message) {
             if (endpoint->queue_count == 0)
                 return false;

             *message = endpoint->queue[endpoint->queue_head];
             endpoint->queue_head = (endpoint->queue_head + 1U) % IPC_QUEUE_CAPACITY;
             endpoint->queue_count--;
             return true;
}

/* Caller holds endpoint->lock. */
static void kipc_signal_notify_bindings_locked(ipc_endpoint_t *endpoint) {
             ipc_notify_binding_t *binding;

             for (binding = endpoint->notify_bindings;
                  binding;
                  binding = binding->next)
                 knotify_signal_ref(binding->notify, binding->bits);
}

/* Called only after the binding list has been detached under endpoint->lock. */
static void kipc_free_notify_bindings(ipc_notify_binding_t *bindings) {
             while (bindings) {
                 ipc_notify_binding_t *next = bindings->next;

                 knotify_release(bindings->notify);
                 kfree(bindings);
                 bindings = next;
             }
}


/*
 * Look up an endpoint and take a reference to it.
 *
 * Once an endpoint has been removed from the registry by kipc_destroy(),
 * no new references can be acquired.
 */
static ipc_endpoint_t *kipc_acquire(ipc_handle_t handle) {
                       ipc_endpoint_t *endpoint;
                       uint32_t hashv = (uint32_t)handle;

                       kmutex_lock(&endpoints_lock);

                       HASH_FIND_BYHASHVALUE(hh, endpoints, &handle, sizeof(handle), hashv, endpoint);

                       if (endpoint)
                           endpoint->references++;

                       kmutex_unlock(&endpoints_lock);

                       return endpoint;
}


/*
 * Actually dispose of an endpoint.
 *
 * This can only happen once it has been removed from the global registry
 * and there are no operations still holding references to it.
 */
static void kipc_endpoint_free(ipc_endpoint_t *endpoint) {
            kfree(endpoint);
}


/*
 * Drop an endpoint reference.
 *
 * The registry itself owns one reference for as long as the endpoint is
 * registered. kipc_destroy() removes it from the registry, wakes any blocked
 * operations, then drops that final registry reference.
 */
static void kipc_release(ipc_endpoint_t *endpoint) {
            bool free_endpoint = false;

            kmutex_lock(&endpoints_lock);

            endpoint->references--;

            if (endpoint->is_shutting_down && endpoint->references == 0)
                free_endpoint = true;

            kmutex_unlock(&endpoints_lock);

            if (free_endpoint)
                kipc_endpoint_free(endpoint);
}


void kipc_init(void) {
     endpoints = NULL;
     next_handle = 1;

     kmutex_init(&endpoints_lock);
}


ipc_status_t kipc_create(ipc_handle_t *handle) {
             ipc_endpoint_t *endpoint;

             if (!handle)
                 return IPC_ERR_INVALID;

             endpoint = kmalloc(sizeof(*endpoint));
             if (!endpoint)
                 return IPC_ERR_NO_MEMORY;

             memset(endpoint, 0, sizeof(*endpoint));

             endpoint->queue_head  = 0;
             endpoint->queue_tail  = 0;
             endpoint->queue_count = 0;

	     endpoint->ep_type     = IPC_ENDPOINT_NORMAL;
	     endpoint->subscribers = NULL;

             kmutex_init(&endpoint->lock);
             ksem_init(&endpoint->sender_sem, 0);
             ksem_init(&endpoint->receiver_sem, 0);

             /*
              * The registry owns one reference.
              */
             endpoint->references = 1;
             endpoint->is_shutting_down = false;
             endpoint->waiting_senders = 0;
             endpoint->waiting_receivers = 0;

             kmutex_lock(&endpoints_lock);

             endpoint->handle = next_handle++;
             uint32_t hashv = (uint32_t)endpoint->handle;
             HASH_ADD_BYHASHVALUE(hh, endpoints, handle, sizeof(endpoint->handle), hashv, endpoint);

             kmutex_unlock(&endpoints_lock);

             *handle = endpoint->handle;

             return IPC_OK;
}

ipc_status_t kipc_create_publisher(ipc_handle_t* handle) {
	     ipc_handle_t new_ep_handle;
	     ipc_status_t status;
	     status = kipc_create(&new_ep_handle);
	     if(status != IPC_OK) return status;
	     
	     ipc_endpoint_t* new_ep = kipc_acquire(new_ep_handle);
	     if(!new_ep) return IPC_ERR_INVALID; // this shouldn't really happen
	    
	     new_ep->ep_type = IPC_ENDPOINT_PUBLISHER;
	     kipc_release(new_ep);
             *handle = new_ep_handle;
	     return IPC_OK;
}

ipc_status_t kipc_subscribe(ipc_handle_t publisher, ipc_handle_t* new_subscriber) {
	     ipc_endpoint_t *pub = kipc_acquire(publisher);

	     if(!pub) {
		return IPC_ERR_NOT_FOUND;
	     }
   	     if(pub->is_shutting_down) {
	        kipc_release(pub);
		return IPC_ERR_ENDPOINT_CLOSED;
	     }
	     if(pub->ep_type != IPC_ENDPOINT_PUBLISHER) {
	        kipc_release(pub);
		return IPC_ERR_INVALID;
	     }

	     ipc_handle_t new_sub_handle;
	     ipc_status_t status;

	     status = kipc_create(&new_sub_handle);
             if(status != IPC_OK) {
		kipc_release(pub);
		return status;
	     }

	     ipc_endpoint_t* new_sub = kipc_acquire(new_sub_handle);
	     if(!new_sub) {
		kipc_release(pub); // TODO - should we cleanup new_sub here?
		return IPC_ERR_INVALID;
	     }
	     
	     new_sub->ep_type = IPC_ENDPOINT_SUBSCRIBER;
	     ipc_subscription_t *subscription = kmalloc(sizeof(*subscription));
	     if (!subscription) {
		kipc_release(new_sub);
		kipc_release(pub);
		kipc_destroy(new_sub_handle);
		return IPC_ERR_NO_MEMORY;
	     }

	     subscription->subscriber = new_sub_handle;
	     subscription->next = NULL;

	     kmutex_lock(&pub->lock);

             if (pub->is_shutting_down) {
                kmutex_unlock(&pub->lock);
                kfree(subscription);
                kipc_release(new_sub);
                kipc_release(pub);
                kipc_destroy(new_sub_handle);
                return IPC_ERR_ENDPOINT_CLOSED;
             }

	     if (!pub->subscribers) {
		pub->subscribers = subscription;
	     } else {
		subscription->next = pub->subscribers;
		pub->subscribers   = subscription;
	     }
	     kmutex_unlock(&pub->lock);

	     *new_subscriber = new_sub_handle;
	     kipc_release(new_sub);
	     kipc_release(pub);
	     return IPC_OK;
}

ipc_status_t kipc_destroy(ipc_handle_t handle) {
             ipc_endpoint_t *endpoint;
             ipc_notify_binding_t *notify_bindings;
             size_t wake_senders;
             size_t wake_receivers;
             size_t i;
             uint32_t hashv = (uint32_t)handle;

             if (!handle)
                 return IPC_ERR_INVALID;

             /*
              * We deliberately do this manually rather than through
              * kipc_acquire(), because removal from the registry must be
              * atomic with respect to new acquisitions.
              */
             kmutex_lock(&endpoints_lock);

             HASH_FIND_BYHASHVALUE(hh, endpoints, &handle, sizeof(handle), hashv, endpoint);

             if (!endpoint) {
                 kmutex_unlock(&endpoints_lock);
                 return IPC_ERR_NOT_FOUND;
             }

             kmutex_lock(&endpoint->lock);


             /*
              * From this point onward no operation should begin or continue.
              */
             endpoint->is_shutting_down = true;

             HASH_DEL(endpoints, endpoint);

             notify_bindings = endpoint->notify_bindings;
             endpoint->notify_bindings = NULL;

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

             kmutex_unlock(&endpoint->lock);
             kmutex_unlock(&endpoints_lock);

             /*
              * Wake everyone blocked waiting for queue state to change.
              * They will re-enter their operation, see is_shutting_down,
              * and return IPC_ERR_ENDPOINT_CLOSED.
              */
             for (i = 0; i < wake_senders; i++)
                 ksem_post(&endpoint->sender_sem);

             for (i = 0; i < wake_receivers; i++)
                 ksem_post(&endpoint->receiver_sem);

             kipc_free_notify_bindings(notify_bindings);

             /*
              * Drop the reference which belonged to the registry.
              *
              * Active send/recv operations each hold their own reference, so
              * the endpoint cannot disappear until every woken operation has
              * returned through kipc_release().
              */
             kipc_release(endpoint);

             return IPC_OK;
}

ipc_status_t kipc_bind_notify(ipc_handle_t handle,
                             notify_handle_t notify_handle,
                             uint64_t bits) {
             ipc_endpoint_t *endpoint;
             ipc_notify_binding_t *binding;
             ipc_notify_binding_t *new_binding;
             notify_t *notify;

             if (!handle || handle == IPC_INVALID_HANDLE ||
                 notify_handle == NOTIFY_INVALID_HANDLE || bits == 0)
                 return IPC_ERR_INVALID;

             notify = knotify_acquire(notify_handle);
             if (!notify)
                 return IPC_ERR_NOT_FOUND;

             new_binding = kmalloc(sizeof(*new_binding));
             if (!new_binding) {
                 knotify_release(notify);
                 return IPC_ERR_NO_MEMORY;
             }

             new_binding->notify = notify;
             new_binding->bits = bits;
             new_binding->next = NULL;

             endpoint = kipc_acquire(handle);
             if (!endpoint) {
                 kfree(new_binding);
                 knotify_release(notify);
                 return IPC_ERR_NOT_FOUND;
             }

             kmutex_lock(&endpoint->lock);

             if (endpoint->is_shutting_down) {
                 kmutex_unlock(&endpoint->lock);
                 kipc_release(endpoint);
                 kfree(new_binding);
                 knotify_release(notify);
                 return IPC_ERR_ENDPOINT_CLOSED;
             }

             for (binding = endpoint->notify_bindings;
                  binding;
                  binding = binding->next) {
                 if (binding->notify == notify) {
                     binding->bits = bits;
                     if (endpoint->queue_count != 0)
                         knotify_signal_ref(notify, bits);
                     kmutex_unlock(&endpoint->lock);
                     kipc_release(endpoint);
                     kfree(new_binding);
                     knotify_release(notify);
                     return IPC_OK;
                 }
             }

             new_binding->next = endpoint->notify_bindings;
             endpoint->notify_bindings = new_binding;
             if (endpoint->queue_count != 0)
                 knotify_signal_ref(notify, bits);

             kmutex_unlock(&endpoint->lock);
             kipc_release(endpoint);
             return IPC_OK;
}

ipc_status_t kipc_unbind_notify(ipc_handle_t handle,
                               notify_handle_t notify_handle) {
             ipc_endpoint_t *endpoint;
             ipc_notify_binding_t **current;
             ipc_notify_binding_t *binding;

             if (!handle || handle == IPC_INVALID_HANDLE ||
                 notify_handle == NOTIFY_INVALID_HANDLE)
                 return IPC_ERR_INVALID;

             endpoint = kipc_acquire(handle);
             if (!endpoint)
                 return IPC_ERR_NOT_FOUND;

             kmutex_lock(&endpoint->lock);

             if (endpoint->is_shutting_down) {
                 kmutex_unlock(&endpoint->lock);
                 kipc_release(endpoint);
                 return IPC_ERR_ENDPOINT_CLOSED;
             }

             current = &endpoint->notify_bindings;
             while (*current && (*current)->notify->handle != notify_handle)
                 current = &(*current)->next;

             binding = *current;
             if (!binding) {
                 kmutex_unlock(&endpoint->lock);
                 kipc_release(endpoint);
                 return IPC_ERR_NOT_FOUND;
             }

             *current = binding->next;
             binding->next = NULL;

             kmutex_unlock(&endpoint->lock);

             knotify_release(binding->notify);
             kfree(binding);
             kipc_release(endpoint);
             return IPC_OK;
}


/*
 * Queue a message on an already referenced endpoint. A full queue waits for
 * a receiver to make room. The caller owns and releases the endpoint reference.
 */
static ipc_status_t kipc_enqueue_blocking(ipc_endpoint_t *endpoint,
                                         const ipc_message_t *queued) {
             for (;;) {
                 kmutex_lock(&endpoint->lock);

                 if (endpoint->is_shutting_down) {
                     kmutex_unlock(&endpoint->lock);
                     return IPC_ERR_ENDPOINT_CLOSED;
                 }

                 if (kipc_queue_push(endpoint, queued)) {
                     kipc_signal_notify_bindings_locked(endpoint);

                     /*
                      * Wake exactly one receiver if one is waiting.
                      *
                      * Decrementing the counter here means repeated sends
                      * cannot build up stale semaphore posts for the same
                      * waiter.
                      */
                     if (endpoint->waiting_receivers) {
                         endpoint->waiting_receivers--;
                         ksem_post(&endpoint->receiver_sem);
                     }

                     kmutex_unlock(&endpoint->lock);
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

                 kmutex_unlock(&endpoint->lock);

                 ksem_wait(&endpoint->sender_sem);

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

/* Queue without waiting for space. The caller holds an endpoint reference. */
static ipc_status_t kipc_enqueue_nonblocking(ipc_endpoint_t *endpoint,
                                            const ipc_message_t *queued) {
             kmutex_lock(&endpoint->lock);

             if (endpoint->is_shutting_down) {
                 kmutex_unlock(&endpoint->lock);
                 return IPC_ERR_ENDPOINT_CLOSED;
             }

             if (!kipc_queue_push(endpoint, queued)) {
                 kmutex_unlock(&endpoint->lock);
                 return IPC_ERR_CANCELLED;
             }

             kipc_signal_notify_bindings_locked(endpoint);

             if (endpoint->waiting_receivers) {
                 endpoint->waiting_receivers--;
                 ksem_post(&endpoint->receiver_sem);
             }

             kmutex_unlock(&endpoint->lock);
             return IPC_OK;
}

/* Subscription links are only prepended; no current path removes or frees one. */
static ipc_status_t kipc_publish(ipc_endpoint_t *publisher,
                                const ipc_message_t *queued) {
             ipc_subscription_t *subscription = NULL;

             for (;;) {
                 ipc_handle_t subscriber_handle;
                 ipc_endpoint_t *subscriber;

                 kmutex_lock(&publisher->lock);
                 if (publisher->is_shutting_down) {
                     kmutex_unlock(&publisher->lock);
                     return IPC_ERR_ENDPOINT_CLOSED;
                 }

                 subscription = subscription ? subscription->next
                                             : publisher->subscribers;
                 if (!subscription) {
                     kmutex_unlock(&publisher->lock);
                     return IPC_OK;
                 }
                 subscriber_handle = subscription->subscriber;
                 kmutex_unlock(&publisher->lock);

                 subscriber = kipc_acquire(subscriber_handle);
                 if (!subscriber)
                     continue;

                 if (subscriber->ep_type == IPC_ENDPOINT_SUBSCRIBER)
                     (void)kipc_enqueue_blocking(subscriber, queued); // TODO - we need policy here!

                 kipc_release(subscriber);
             }
}

/*
 * Blocking send to a normal endpoint, or non-blocking fan-out from a
 * publisher. Direct sends to subscriber endpoints are rejected.
 */
ipc_status_t kipc_send(thread_t *caller, ipc_handle_t handle, const ipc_message_t *message) {
             ipc_endpoint_t *endpoint;
             ipc_message_t queued;
             ipc_status_t status;

             if (!caller || !handle || !message)
                 return IPC_ERR_INVALID;

             endpoint = kipc_acquire(handle);
             if (!endpoint)
                 return IPC_ERR_NOT_FOUND;

             if (endpoint->ep_type == IPC_ENDPOINT_SUBSCRIBER) {
                 kipc_release(endpoint);
                 return IPC_ERR_INVALID;
             }

             queued = *message;
             queued.sender_tid = caller->id;

             if (endpoint->ep_type == IPC_ENDPOINT_PUBLISHER)
                 status = kipc_publish(endpoint, &queued);
             else
                 status = kipc_enqueue_blocking(endpoint, &queued);

             kipc_release(endpoint);
             return status;
}


/*
 * Non-blocking send.
 *
 * A full queue returns IPC_ERR_CANCELLED.
 */
ipc_status_t kipc_send_nb(thread_t *caller, ipc_handle_t handle, const ipc_message_t *message) {
             ipc_endpoint_t *endpoint;
             ipc_message_t queued;
             ipc_status_t status;

             if (!caller || !handle || !message)
                 return IPC_ERR_INVALID;

             endpoint = kipc_acquire(handle);
             if (!endpoint)
                 return IPC_ERR_NOT_FOUND;

             queued = *message;
             queued.sender_tid = caller->id;
             status = kipc_enqueue_nonblocking(endpoint, &queued);
             kipc_release(endpoint);
             return status;
}


/*
 * Blocking receive.
 *
 */
ipc_status_t kipc_recv(ipc_handle_t handle, ipc_message_t *message) {
             ipc_endpoint_t *endpoint;

             if ( !handle || !message)
                 return IPC_ERR_INVALID;

             endpoint = kipc_acquire(handle);
             if (!endpoint)
                 return IPC_ERR_NOT_FOUND;

             for (;;) {
                 kmutex_lock(&endpoint->lock);


                 if (endpoint->is_shutting_down) {
                     kmutex_unlock(&endpoint->lock);
                     kipc_release(endpoint);

                     return IPC_ERR_ENDPOINT_CLOSED;
                 }

                 if (kipc_queue_pop(endpoint, message)) {
                     /*
                      * One queue slot just became available.
                      */
                     if (endpoint->waiting_senders) {
                         endpoint->waiting_senders--;
                         ksem_post(&endpoint->sender_sem);
                     }

                     kmutex_unlock(&endpoint->lock);

                     kipc_release(endpoint);

                     return IPC_OK;
                 }

                 /*
                  * Nothing available. Register the wait before dropping the
                  * endpoint mutex, then sleep.
                  */
                 endpoint->waiting_receivers++;

                 kmutex_unlock(&endpoint->lock);

                 ksem_wait(&endpoint->receiver_sem);
             }
}


/*
 * Non-blocking receive.
 *
 * Empty queue returns IPC_ERR_CANCELLED.
 */
ipc_status_t kipc_recv_nb(ipc_handle_t handle, ipc_message_t *message) {
             ipc_endpoint_t *endpoint;

             if (!handle || !message)
                 return IPC_ERR_INVALID;

             endpoint = kipc_acquire(handle);
             if (!endpoint)
                 return IPC_ERR_NOT_FOUND;

             kmutex_lock(&endpoint->lock);

             if (endpoint->is_shutting_down) {
                 kmutex_unlock(&endpoint->lock);
                 kipc_release(endpoint);

                 return IPC_ERR_ENDPOINT_CLOSED;
             }

             if (!kipc_queue_pop(endpoint, message)) {
                 kmutex_unlock(&endpoint->lock);
                 kipc_release(endpoint);

                 return IPC_ERR_CANCELLED;
             }

             if (endpoint->waiting_senders) {
                 endpoint->waiting_senders--;
                 ksem_post(&endpoint->sender_sem);
             }

             kmutex_unlock(&endpoint->lock);

             kipc_release(endpoint);

             return IPC_OK;
}
