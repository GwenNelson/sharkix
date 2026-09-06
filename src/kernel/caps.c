#include <sharkix/kernel/caps.h>
#include <sharkix/kernel/memory.h>
#include <stdint.h>
#include <stdbool.h>
 
#include <libfifo/sync.h>
#include <sharkix/kernel/uthash.h>
 
static cap_handle_t next_cap_handle = 0;
static cap_t*       global_caps_table = NULL;
static fifo_mutex_t global_caps_table_lock;

static capset_handle_t next_capset_handle = 1;
static capset_t       *global_capsets_table = NULL;
static fifo_mutex_t    global_capsets_table_lock;

static int kcap_validate_rights(cap_type_t type, cap_rights_t rights) {
    switch (type) {
    case CAP_TYPE_IPC_ENDPOINT:
        return (rights & ~CAP_IPC_VALID_RIGHTS) ? -1 : 0;

    default:
        return -1;
    }
}

static capset_t *kcapset_find_locked(capset_handle_t handle)
{
    capset_t *set = NULL;

    HASH_FIND(hh,
              global_capsets_table,
              &handle,
              sizeof(handle),
              set);

    return set;
}

void kinit_caps(void)
{
    global_caps_table    = NULL;
    global_capsets_table = NULL;

    next_cap_handle    = 1;
    next_capset_handle = 1;

    fifo_mutex_init(&global_caps_table_lock);
    fifo_mutex_init(&global_capsets_table_lock);

    // we have to do this here, cos it can't be done before the memory system is live
    kcapset_new(&(address_space_kernel()->capset));
    // yes, we should probably verify that it worked, and then panic - i need to implement a proper kpanic first!
}

int kcap_create(kobject_handle_t obj_handle,
                cap_type_t cap_type,
                cap_rights_t init_rights,
                cap_handle_t *new_cap)
{
    cap_t *cap;

    if (!new_cap)
        return -1;

    if (kcap_validate_rights(cap_type, init_rights) < 0)
        return -1;

    cap = kmalloc(sizeof(*cap));
    if (!cap)
        return -1;

    memset(cap, 0, sizeof(*cap));

    cap->type       = cap_type;
    cap->obj_handle = obj_handle;
    cap->rights     = init_rights;

    fifo_mutex_lock(&global_caps_table_lock);

    cap->cap_handle = next_cap_handle++;

    HASH_ADD(hh,
             global_caps_table,
             cap_handle,
             sizeof(cap->cap_handle),
             cap);

    fifo_mutex_unlock(&global_caps_table_lock);

    *new_cap = cap->cap_handle;
    return 0;
}

bool kcap_cap_exists(cap_handle_t handle) {
    cap_t *found = NULL;

    fifo_mutex_lock(&global_caps_table_lock);

    HASH_FIND(hh,
              global_caps_table,
              &handle,
              sizeof(handle),
              found);

    fifo_mutex_unlock(&global_caps_table_lock);

    return found != NULL;
}

int kcap_destroy(cap_handle_t handle) {
    cap_t *found = NULL;

    fifo_mutex_lock(&global_caps_table_lock);

    HASH_FIND(hh,
              global_caps_table,
              &handle,
              sizeof(handle),
              found);

    if (!found) {
        fifo_mutex_unlock(&global_caps_table_lock);
        return -1;
    }

    HASH_DEL(global_caps_table, found);

    fifo_mutex_unlock(&global_caps_table_lock);

    kfree(found);
    return 0;
}

int kcap_derive(cap_handle_t source,
                cap_rights_t new_rights,
                cap_handle_t *new_cap)
{
    cap_t *src = NULL;
    cap_t *derived;

    if (!new_cap)
        return -1;

    derived = kmalloc(sizeof(*derived));
    if (!derived)
        return -1;

    memset(derived, 0, sizeof(*derived));

    fifo_mutex_lock(&global_caps_table_lock);

    HASH_FIND(hh,
              global_caps_table,
              &source,
              sizeof(source),
              src);

    if (!src) {
        fifo_mutex_unlock(&global_caps_table_lock);
        kfree(derived);
        return -1;
    }

    /*
     * Requested rights must be a subset of the source rights.
     */
    if ((new_rights & src->rights) != new_rights) {
        fifo_mutex_unlock(&global_caps_table_lock);
        kfree(derived);
        return -1;
    }

    derived->cap_handle = next_cap_handle++;
    derived->type       = src->type;
    derived->obj_handle = src->obj_handle;
    derived->rights     = new_rights;

    HASH_ADD(hh,
             global_caps_table,
             cap_handle,
             sizeof(derived->cap_handle),
             derived);

    fifo_mutex_unlock(&global_caps_table_lock);

    *new_cap = derived->cap_handle;
    return 0;
}

int kcap_clone(cap_handle_t source, cap_handle_t *new_cap)
{
    cap_t *src = NULL;
    cap_t *clone;

    if (!new_cap)
        return -1;

    clone = kmalloc(sizeof(*clone));
    if (!clone)
        return -1;

    memset(clone, 0, sizeof(*clone));

    fifo_mutex_lock(&global_caps_table_lock);

    HASH_FIND(hh,
              global_caps_table,
              &source,
              sizeof(source),
              src);

    if (!src) {
        fifo_mutex_unlock(&global_caps_table_lock);
        kfree(clone);
        return -1;
    }

    clone->cap_handle = next_cap_handle++;
    clone->type       = src->type;
    clone->obj_handle = src->obj_handle;
    clone->rights     = src->rights;

    HASH_ADD(hh,
             global_caps_table,
             cap_handle,
             sizeof(clone->cap_handle),
             clone);

    fifo_mutex_unlock(&global_caps_table_lock);

    *new_cap = clone->cap_handle;
    return 0;
}

int kcap_merge(cap_handle_t a,
               cap_handle_t b,
               cap_handle_t *new_cap)
{
    cap_t *cap_a = NULL;
    cap_t *cap_b = NULL;
    cap_t *merged;

    if (!new_cap)
        return -1;

    merged = kmalloc(sizeof(*merged));
    if (!merged)
        return -1;

    memset(merged, 0, sizeof(*merged));

    fifo_mutex_lock(&global_caps_table_lock);

    HASH_FIND(hh,
              global_caps_table,
              &a,
              sizeof(a),
              cap_a);

    HASH_FIND(hh,
              global_caps_table,
              &b,
              sizeof(b),
              cap_b);

    if (!cap_a || !cap_b) {
        fifo_mutex_unlock(&global_caps_table_lock);
        kfree(merged);
        return -1;
    }

    if (!CAPS_SAME_OBJECT(cap_a, cap_b) ||
        !CAP_IS_TYPE(cap_a, cap_b->type)) {
        fifo_mutex_unlock(&global_caps_table_lock);
        kfree(merged);
        return -1;
    }

    merged->cap_handle = next_cap_handle++;
    merged->type       = cap_a->type;
    merged->obj_handle = cap_a->obj_handle;
    merged->rights     = cap_a->rights | cap_b->rights;

    /*
     * Defensive sanity check. This should naturally succeed if both
     * source caps were valid for this type.
     */
    if (kcap_validate_rights(merged->type, merged->rights) < 0) {
        fifo_mutex_unlock(&global_caps_table_lock);
        kfree(merged);
        return -1;
    }

    HASH_ADD(hh,
             global_caps_table,
             cap_handle,
             sizeof(merged->cap_handle),
             merged);

    fifo_mutex_unlock(&global_caps_table_lock);

    *new_cap = merged->cap_handle;
    return 0;
}

int kcap_getcap(cap_handle_t handle, cap_t *out) {
    cap_t *found = NULL;

    if (!out)
        return -1;

    fifo_mutex_lock(&global_caps_table_lock);

    HASH_FIND(hh,
              global_caps_table,
              &handle,
              sizeof(handle),
              found);

    if (!found) {
        fifo_mutex_unlock(&global_caps_table_lock);
        return -1;
    }

    /*
     * Don't memcpy uthash internals/spinlocks as meaningful state.
     * Copy only capability data.
     */
    out->cap_handle = found->cap_handle;
    out->type       = found->type;
    out->obj_handle = found->obj_handle;
    out->rights     = found->rights;

    fifo_mutex_unlock(&global_caps_table_lock);

    return 0;
}

int kcapset_new(capset_handle_t *new_set)
{
    capset_t *set;

    if (!new_set)
        return -1;

    set = kmalloc(sizeof(*set));
    if (!set)
        return -1;

    memset(set, 0, sizeof(*set));
    fifo_spinlock_init(&set->spinlock);

    fifo_mutex_lock(&global_capsets_table_lock);

    set->capset_handle = next_capset_handle++;

    HASH_ADD(hh,
             global_capsets_table,
             capset_handle,
             sizeof(set->capset_handle),
             set);

    fifo_mutex_unlock(&global_capsets_table_lock);

    *new_set = set->capset_handle;
    return 0;
}

int kcapset_addcap(capset_handle_t set_handle, cap_handle_t cap_handle)
{
    capset_t *set;
    capset_entry_t *existing = NULL;
    capset_entry_t *entry;

    if (!kcap_cap_exists(cap_handle))
        return -1;

    entry = kmalloc(sizeof(*entry));
    if (!entry)
        return -1;

    memset(entry, 0, sizeof(*entry));
    entry->cap_handle = cap_handle;

    fifo_mutex_lock(&global_capsets_table_lock);

    set = kcapset_find_locked(set_handle);
    if (!set) {
        fifo_mutex_unlock(&global_capsets_table_lock);
        kfree(entry);
        return -1;
    }

    /*
     * If capsets cannot yet be destroyed, dropping this mutex here is
     * okay because `set` stays alive.
     */
    fifo_mutex_unlock(&global_capsets_table_lock);

    fifo_spinlock_lock(&set->spinlock);

    HASH_FIND(hh,
              set->caps,
              &cap_handle,
              sizeof(cap_handle),
              existing);

    if (existing) {
        fifo_spinlock_unlock(&set->spinlock);
        kfree(entry);
        return -1;
    }

    HASH_ADD(hh,
             set->caps,
             cap_handle,
             sizeof(entry->cap_handle),
             entry);

    fifo_spinlock_unlock(&set->spinlock);

    return 0;
}

int kcapset_delcap(capset_handle_t set_handle, cap_handle_t cap_handle)
{
    capset_t *set;
    capset_entry_t *entry = NULL;

    fifo_mutex_lock(&global_capsets_table_lock);

    set = kcapset_find_locked(set_handle);
    if (!set) {
        fifo_mutex_unlock(&global_capsets_table_lock);
        return -1;
    }

    fifo_mutex_unlock(&global_capsets_table_lock);

    fifo_spinlock_lock(&set->spinlock);

    HASH_FIND(hh,
              set->caps,
              &cap_handle,
              sizeof(cap_handle),
              entry);

    if (!entry) {
        fifo_spinlock_unlock(&set->spinlock);
        return -1;
    }

    HASH_DEL(set->caps, entry);

    fifo_spinlock_unlock(&set->spinlock);

    kfree(entry);
    return 0;
}

bool kcapset_hascap(capset_handle_t set_handle, cap_handle_t cap_handle)
{
    capset_t *set;
    capset_entry_t *entry = NULL;

    fifo_mutex_lock(&global_capsets_table_lock);

    set = kcapset_find_locked(set_handle);
    if (!set) {
        fifo_mutex_unlock(&global_capsets_table_lock);
        return false;
    }

    fifo_mutex_unlock(&global_capsets_table_lock);

    fifo_spinlock_lock(&set->spinlock);

    HASH_FIND(hh,
              set->caps,
              &cap_handle,
              sizeof(cap_handle),
              entry);

    fifo_spinlock_unlock(&set->spinlock);

    return entry != NULL;
}

int kcapset_resolve_cap(capset_handle_t set_handle,
                        cap_type_t req_type,
                        cap_rights_t req_rights,
                        cap_t *out)
{
    capset_t *set;
    capset_entry_t *entry, *tmp;
    cap_t cap;

    fifo_mutex_lock(&global_capsets_table_lock);

    set = kcapset_find_locked(set_handle);

    if (!set) {
        fifo_mutex_unlock(&global_capsets_table_lock);
        return -1;
    }

    fifo_mutex_unlock(&global_capsets_table_lock);

    fifo_spinlock_lock(&set->spinlock);

    HASH_ITER(hh, set->caps, entry, tmp) {
        if (kcap_getcap(entry->cap_handle, &cap) < 0)
            continue;

        if (!CAP_IS_TYPE(&cap, req_type))
            continue;

        if (!CAP_HAS_ALL(&cap, req_rights))
            continue;

        if (out) {
            out->cap_handle = cap.cap_handle;
            out->type       = cap.type;
            out->obj_handle = cap.obj_handle;
            out->rights     = cap.rights;
        }

        fifo_spinlock_unlock(&set->spinlock);
        return 0;
    }

    fifo_spinlock_unlock(&set->spinlock);

    return -1;
}

int kcapset_resolve_handle(capset_handle_t set_handle,
                           cap_handle_t cap_handle,
                           cap_type_t required_type,
                           cap_rights_t required_rights,
                           kobject_handle_t *out)
{
    capset_t *set;
    capset_entry_t *entry = NULL;
    cap_t cap;

    if (!out)
        return -1;

    fifo_mutex_lock(&global_capsets_table_lock);

    set = kcapset_find_locked(set_handle);
    if (!set) {
        fifo_mutex_unlock(&global_capsets_table_lock);
        return -1;
    }

    fifo_mutex_unlock(&global_capsets_table_lock);

    fifo_spinlock_lock(&set->spinlock);

    HASH_FIND(hh,
              set->caps,
              &cap_handle,
              sizeof(cap_handle),
              entry);

    if (!entry || kcap_getcap(cap_handle, &cap) != 0 ||
        !CAP_IS_TYPE(&cap, required_type) ||
        !CAP_HAS_ALL(&cap, required_rights)) {
        fifo_spinlock_unlock(&set->spinlock);
        return -1;
    }

    *out = cap.obj_handle;

    fifo_spinlock_unlock(&set->spinlock);
    return 0;
}

int kcapset_check_perms(capset_handle_t set,
                        cap_type_t req_type,
                        cap_rights_t req_rights)
{
    return kcapset_resolve_cap(set,
                               req_type,
                               req_rights,
                               NULL);
}
