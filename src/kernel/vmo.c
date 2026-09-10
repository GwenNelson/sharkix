#include <sharkix/kernel/vmo.h>

#include <stdint.h>
#include <stddef.h>

#include <sharkix/kernel/kmalloc.h>
#include <sharkix/kernel/sync.h>


/*
 * Global VMO and VMO-set tables.
 */
static vmo_t    *vmos;
static vmoset_t *vmosets;

static kspinlock_t vmos_lock;
static kspinlock_t vmosets_lock;

static vmo_handle_t    next_vmo_handle    = 1;
static vmoset_handle_t next_vmoset_handle = 1;


/*
 * Internal lookup helpers.
 *
 * Caller must hold the corresponding global table lock.
 */
static vmo_t *vmo_lookup_locked(vmo_handle_t handle)
{
    vmo_t *vmo = NULL;

    HASH_FIND(hh, vmos, &handle, sizeof(handle), vmo);

    return vmo;
}


static vmoset_t *vmoset_lookup_locked(vmoset_handle_t handle)
{
    vmoset_t *set = NULL;

    HASH_FIND(hh, vmosets, &handle, sizeof(handle), set);

    return set;
}


/*
 * Initialise the VMO subsystem.
 */
void kvmo_init(void)
{
    vmos = NULL;
    vmosets = NULL;

    next_vmo_handle = 1;
    next_vmoset_handle = 1;

    /*
     * Use your actual spinlock-init functions here.
     */
    kspin_init(&vmos_lock);
    kspin_init(&vmosets_lock);
}


/*
 * Create a new VMO.
 *
 * Capability/security policy is deliberately NOT enforced here.
 */
int kvmo_create(vmo_handle_t *out,
                pmem_handle_t pmem,
                vmo_rights_t rights)
{
    vmo_t *vmo;

    if (out == NULL)
        return -1;

    if (rights & ~VMO_VALID_RIGHTS)
        return -1;

    vmo = kmalloc(sizeof(*vmo));
    if (vmo == NULL)
        return -1;

    vmo->pmem = pmem;
    vmo->rights = rights;

    kspin_lock(&vmos_lock);

    /*
     * For now monotonically allocate handles.
     *
     * Eventually, if handles can wrap/reuse, this needs the same sort of
     * generation/reuse treatment as your other kernel handle tables.
     */
    vmo->handle = next_vmo_handle++;

    HASH_ADD(hh, vmos, handle, sizeof(vmo->handle), vmo);

    kspin_unlock(&vmos_lock);

    *out = vmo->handle;

    return 0;
}


/*
 * Return a descriptive copy of a VMO.
 */
int kvmo_get(vmo_handle_t handle, vmo_t *out)
{
    vmo_t *vmo;

    if (out == NULL)
        return -1;

    if (handle == VMO_INVALID_HANDLE)
        return -1;

    kspin_lock(&vmos_lock);

    vmo = vmo_lookup_locked(handle);
    if (vmo == NULL) {
        kspin_unlock(&vmos_lock);
        return -1;
    }

    /*
     * Don't copy uthash's internal linkage state into something that may
     * subsequently be mistaken for a table member.
     */
    out->handle = vmo->handle;
    out->rights = vmo->rights;
    out->pmem = vmo->pmem;

    kspin_unlock(&vmos_lock);

    return 0;
}


/*
 * Destroy the VMO object only.
 *
 * This does NOT:
 *   - unmap it
 *   - destroy its PMEM backing
 *   - remove caps pointing at it
 */
int kvmo_destroy(vmo_handle_t handle)
{
    vmo_t *vmo;

    if (handle == VMO_INVALID_HANDLE)
        return -1;

    kspin_lock(&vmos_lock);

    vmo = vmo_lookup_locked(handle);
    if (vmo == NULL) {
        kspin_unlock(&vmos_lock);
        return -1;
    }

    HASH_DEL(vmos, vmo);

    kspin_unlock(&vmos_lock);

    kfree(vmo);

    return 0;
}


/*
 * Create an empty VMO set.
 */
int kvmoset_new(vmoset_handle_t *out)
{
    vmoset_t *set;

    if (out == NULL)
        return -1;

    set = kmalloc(sizeof(*set));
    if (set == NULL)
        return -1;

    set->entries = NULL;

    kspin_init(&set->spinlock);

    kspin_lock(&vmosets_lock);

    set->handle = next_vmoset_handle++;

    HASH_ADD(hh, vmosets, handle, sizeof(set->handle), set);

    kspin_unlock(&vmosets_lock);

    *out = set->handle;

    return 0;
}


/*
 * Destroy a VMO set and its bookkeeping entries.
 *
 * IMPORTANT:
 *
 * This does NOT unmap anything.
 * This does NOT destroy any VMO objects.
 *
 * Address-space teardown must call kvmoset_unmap_all() first if the
 * mappings represented by this set are still live.
 */
int kvmoset_destroy(vmoset_handle_t handle)
{
    vmoset_t *set;
    vmoset_entry_t *entry;
    vmoset_entry_t *tmp;

    if (handle == VMOSET_INVALID_HANDLE)
        return -1;

    kspin_lock(&vmosets_lock);

    set = vmoset_lookup_locked(handle);
    if (set == NULL) {
        kspin_unlock(&vmosets_lock);
        return -1;
    }

    HASH_DEL(vmosets, set);

    kspin_unlock(&vmosets_lock);

    /*
     * At this point nobody should be able to newly resolve the set from
     * the global table.
     *
     * The caller is responsible for ensuring the set isn't concurrently
     * in use while it is being destroyed.
     */
    HASH_ITER(hh, set->entries, entry, tmp) {
        HASH_DEL(set->entries, entry);
        kfree(entry);
    }

    kfree(set);

    return 0;
}


/*
 * Add one mapping description to a VMO set.
 *
 * This function performs bookkeeping only.  It does not modify page
 * tables.
 */
int kvmoset_add(vmoset_handle_t handle,
                vmo_handle_t vmo,
                uintptr_t va,
                size_t offset,
                size_t length,
                vmo_rights_t rights)
{
    vmoset_t *set;
    vmoset_entry_t *entry;
    vmoset_entry_t *iter;
    uintptr_t new_end;

    if (handle == VMOSET_INVALID_HANDLE)
        return -1;

    if (vmo == VMO_INVALID_HANDLE)
        return -1;

    if (length == 0)
        return -1;

    if (rights & ~(VMO_READ | VMO_WRITE | VMO_EXEC))
        return -1;

    /*
     * Mappings are represented as half-open intervals [va, va + length).
     * Reject ranges whose end would wrap around the address space.
     */
    if (length > UINTPTR_MAX - va)
        return -1;

    new_end = va + length;

    entry = kmalloc(sizeof(*entry));
    if (entry == NULL)
        return -1;

    entry->virtual_address = va;
    entry->length = length;
    entry->vmo = vmo;
    entry->offset = offset;
    entry->rights = rights;

    /*
     * Lock ordering:
     *
     *     vmosets_lock -> set->spinlock
     *
     * Holding vmosets_lock while acquiring the set lock prevents the set
     * from disappearing between lookup and acquisition of its lock.
     */
    kspin_lock(&vmosets_lock);

    set = vmoset_lookup_locked(handle);
    if (set == NULL) {
        kspin_unlock(&vmosets_lock);
        kfree(entry);
        return -1;
    }

    kspin_lock(&set->spinlock);
    kspin_unlock(&vmosets_lock);

    /*
     * Reject any intersection with an existing mapping.
     *
     * Two half-open intervals overlap iff:
     *
     *     new_start < old_end && old_start < new_end
     *
     * Adjacent mappings are therefore permitted.
     */
    for (iter = set->entries; iter != NULL; iter = iter->hh.next) {
        uintptr_t old_start = iter->virtual_address;
        uintptr_t old_end;

        /*
         * This should never happen because entries are validated before
         * insertion, but avoid allowing corrupted bookkeeping to make the
         * overlap calculation wrap.
         */
        if (iter->length > UINTPTR_MAX - old_start) {
            kspin_unlock(&set->spinlock);
            kfree(entry);
            return -1;
        }

        old_end = old_start + iter->length;

        if (va < old_end && old_start < new_end) {
            kspin_unlock(&set->spinlock);
            kfree(entry);
            return -1;
        }
    }

    HASH_ADD(hh,
             set->entries,
             virtual_address,
             sizeof(entry->virtual_address),
             entry);

    kspin_unlock(&set->spinlock);

    return 0;
}


/*
 * Remove one mapping-description entry.
 *
 * This function performs bookkeeping only.  It does NOT unmap anything.
 */
int kvmoset_remove(vmoset_handle_t handle,
                   uintptr_t va)
{
    vmoset_t *set;
    vmoset_entry_t *entry;

    kspin_lock(&vmosets_lock);

    set = vmoset_lookup_locked(handle);
    if (set == NULL) {
        kspin_unlock(&vmosets_lock);
        return -1;
    }

    kspin_lock(&set->spinlock);
    kspin_unlock(&vmosets_lock);

    HASH_FIND(hh,
              set->entries,
              &va,
              sizeof(va),
              entry);

    if (entry == NULL) {
        kspin_unlock(&set->spinlock);
        return -1;
    }

    HASH_DEL(set->entries, entry);

    kspin_unlock(&set->spinlock);

    kfree(entry);

    return 0;
}


/*
 * Find the mapping containing 'va'.
 *
 * Unlike the hash key lookup used internally by remove(), this deliberately
 * supports addresses in the middle of a mapping.
 */
int kvmoset_find(vmoset_handle_t handle,
                 uintptr_t va,
                 vmoset_entry_t *out)
{
    vmoset_t *set;
    vmoset_entry_t *entry;

    if (out == NULL)
        return -1;

    kspin_lock(&vmosets_lock);

    set = vmoset_lookup_locked(handle);
    if (set == NULL) {
        kspin_unlock(&vmosets_lock);
        return -1;
    }

    kspin_lock(&set->spinlock);
    kspin_unlock(&vmosets_lock);

    for (entry = set->entries; entry != NULL; entry = entry->hh.next) {
        uintptr_t start = entry->virtual_address;
        uintptr_t end;

        /*
         * Avoid overflow in start + length.
         */
        if (entry->length > UINTPTR_MAX - start)
            continue;

        end = start + entry->length;

        if (va >= start && va < end) {
            out->virtual_address = entry->virtual_address;
            out->length = entry->length;
            out->vmo = entry->vmo;
            out->offset = entry->offset;
            out->rights = entry->rights;

            kspin_unlock(&set->spinlock);
            return 0;
        }
    }

    kspin_unlock(&set->spinlock);

    return -1;
}
