#include <stdint.h>
#include <string.h>

#include <stdbool.h>

#include <sharkix/kernel/subsystems/notification.h>

#include <sharkix/kernel/memory.h>
#include <sharkix/kernel/sync.h>

#include <sharkix/kernel/uthash.h>

static notify_t *global_notify_table;
static notify_handle_t next_notify_handle;
static kmutex_t global_notify_table_lock;

static notify_t *knotify_find_locked(notify_handle_t handle) {
    notify_t *notify = NULL;
    uint32_t hashv = (uint32_t)handle;

    HASH_FIND_BYHASHVALUE(
        hh,
        global_notify_table,
        &handle,
        sizeof(handle),
        hashv,
        notify
    );

    return notify;
}

/* The table lock protects both lookup and the notification reference count. */
notify_t *knotify_acquire(notify_handle_t handle) {
    notify_t *notify;

    if (handle == NOTIFY_INVALID_HANDLE)
        return NULL;

    kmutex_lock(&global_notify_table_lock);
    notify = knotify_find_locked(handle);
    if (notify)
        notify->references++;
    kmutex_unlock(&global_notify_table_lock);

    return notify;
}

void knotify_release(notify_t *notify) {
    bool free_notify = false;

    if (!notify)
        return;

    kmutex_lock(&global_notify_table_lock);
    notify->references--;
    if (notify->references == 0)
        free_notify = true;
    kmutex_unlock(&global_notify_table_lock);

    if (free_notify)
        kfree(notify);
}

void knotify_init(void) {
    global_notify_table = NULL;
    next_notify_handle = 1;
    kmutex_init(&global_notify_table_lock);
}

int knotify_create(notify_handle_t *out) {
    notify_t *notify;

    if (!out)
        return -1;

    notify = kmalloc(sizeof(*notify));
    if (!notify)
        return -1;

    memset(notify, 0, sizeof(*notify));
    notify->references = 1; /* The global table owns the initial reference. */
    kspin_init(&notify->lock);
    ksem_init(&notify->sem, 0);

    kmutex_lock(&global_notify_table_lock);

    if (next_notify_handle == NOTIFY_INVALID_HANDLE) {
        kmutex_unlock(&global_notify_table_lock);
        kfree(notify);
        return -1;
    }

    notify->handle = next_notify_handle++;
    uint32_t hashv = (uint32_t)notify->handle;
    HASH_ADD_BYHASHVALUE(
        hh,
        global_notify_table,
        handle,
        sizeof(notify->handle),
        hashv,
        notify
    );

    kmutex_unlock(&global_notify_table_lock);

    *out = notify->handle;
    return 0;
}

int knotify_destroy(notify_handle_t handle) {
    notify_t *notify;

    if (handle == NOTIFY_INVALID_HANDLE)
        return -1;

    kmutex_lock(&global_notify_table_lock);

    notify = knotify_find_locked(handle);
    if (!notify) {
        kmutex_unlock(&global_notify_table_lock);
        return -1;
    }

    HASH_DEL(global_notify_table, notify);
    kmutex_unlock(&global_notify_table_lock);

    knotify_release(notify);
    return 0;
}

void knotify_signal_ref(notify_t *notify, uint64_t bits) {
    kirq_flags_t flags;

    if (!notify || bits == 0)
        return;

    flags = kspin_lock_irqsave(&notify->lock);
    notify->pending |= bits;
    kspin_unlock_irqrestore(&notify->lock, flags);

    ksem_post(&notify->sem);
}

int knotify_signal(notify_handle_t handle, uint64_t bits) {
    notify_t *notify;

    if (handle == NOTIFY_INVALID_HANDLE)
        return -1;

    notify = knotify_acquire(handle);
    if (!notify)
        return -1;

    knotify_signal_ref(notify, bits);
    knotify_release(notify);
    return 0;
}

int knotify_wait(notify_handle_t handle, uint64_t bits) {
    notify_t *notify;

    if (handle == NOTIFY_INVALID_HANDLE || bits == 0)
        return -1;

    notify = knotify_acquire(handle);

    if (!notify)
        return -1;

    for (;;) {
        kirq_flags_t flags = kspin_lock_irqsave(&notify->lock);
        if (notify->pending & bits) {
            kspin_unlock_irqrestore(&notify->lock, flags);
            knotify_release(notify);
            return 0;
        }
        kspin_unlock_irqrestore(&notify->lock, flags);

        ksem_wait(&notify->sem);
    }
}

int knotify_poll(notify_handle_t handle, uint64_t *out_bits) {
    notify_t *notify;

    if (handle == NOTIFY_INVALID_HANDLE || !out_bits)
        return -1;

    notify = knotify_acquire(handle);

    if (!notify)
        return -1;

    kirq_flags_t flags = kspin_lock_irqsave(&notify->lock);
    *out_bits = notify->pending;
    kspin_unlock_irqrestore(&notify->lock, flags);

    knotify_release(notify);
    return 0;
}

int knotify_ack(notify_handle_t handle, uint64_t bits) {
    notify_t *notify;

    if (handle == NOTIFY_INVALID_HANDLE)
        return -1;

    notify = knotify_acquire(handle);

    if (!notify)
        return -1;

    kirq_flags_t flags = kspin_lock_irqsave(&notify->lock);
    notify->pending &= ~bits;
    kspin_unlock_irqrestore(&notify->lock, flags);

    knotify_release(notify);
    return 0;
}
