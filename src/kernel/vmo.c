#include <sharkix/kernel/vmo.h>

#include <stdint.h>
#include <stddef.h>

#include <sharkix/kernel/kmalloc.h>
#include <sharkix/kernel/sync.h>
#include <sharkix/kernel/pmem.h>
#include <sharkix/kernel/memory.h>

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
    uint32_t hashv = (uint32_t)handle;

    HASH_FIND_BYHASHVALUE(hh, vmos, &handle, sizeof(handle), hashv, vmo);

    return vmo;
}


static vmoset_t *vmoset_lookup_locked(vmoset_handle_t handle)
{
    vmoset_t *set = NULL;
    uint32_t hashv = (uint32_t)handle;

    HASH_FIND_BYHASHVALUE(hh, vmosets, &handle, sizeof(handle), hashv, set);

    return set;
}

int kvmoset_set_rights(vmoset_handle_t handle,
                       uintptr_t va,
                       vmo_rights_t rights)
{
    vmoset_t *set;
    vmoset_entry_t *entry;

    if (handle == VMOSET_INVALID_HANDLE)
        return -1;

    if (rights & ~(VMO_READ | VMO_WRITE | VMO_EXEC))
        return -1;

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

    entry->rights = rights;

    kspin_unlock(&set->spinlock);

    return 0;
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
    uint32_t hashv = (uint32_t)vmo->handle;

    HASH_ADD_BYHASHVALUE(hh, vmos, handle, sizeof(vmo->handle), hashv, vmo);

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
    uint32_t hashv = (uint32_t)set->handle;

    HASH_ADD_BYHASHVALUE(hh, vmosets, handle, sizeof(set->handle), hashv, set);

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

int kvmo_map(vmo_handle_t handle,
             address_space_t *as,
             uintptr_t va,
             size_t offset,
             size_t length,
             vmo_rights_t rights)
{
    vmo_t vmo;
    pmem_t pmem;
    uintptr_t phys;
    uint64_t map_flags;

    if (as == NULL)
        return -1;

    if (handle == VMO_INVALID_HANDLE)
        return -1;

    if (length == 0)
        return -1;

    /*
     * VMO_MAP is an operation right, not a page permission.
     */
    if (rights & ~(VMO_READ | VMO_WRITE | VMO_EXEC))
        return -1;

    if (kvmo_get(handle, &vmo) != 0)
        return -1;

    /*
     * The VMO itself must permit mapping, and the requested mapping
     * permissions must be a subset of its intrinsic rights.
     */
    if (!(vmo.rights & VMO_MAP))
        return -1;

    if (rights & ~vmo.rights)
        return -1;

    /*
     * x86-64 ordinary page tables do not have a readable bit.
     *
     * A present page is inherently readable, so allowing a mapping
     * without VMO_READ would grant more access than was requested.
     *
     * For now, therefore, every mapped VMO must be readable.
     */
    if (!(rights & VMO_READ))
        return -1;

    if (kpmem_get(vmo.pmem, &pmem) != 0)
        return -1;

    /*
     * Validate the requested range within the PMEM backing without
     * allowing offset + length to overflow.
     */
    if (offset > pmem.length)
        return -1;

    if (length > pmem.length - offset)
        return -1;

    /*
     * Don't allow calculation of the physical address to wrap.
     */
    if (offset > UINTPTR_MAX - pmem.phys_base)
        return -1;

    phys = pmem.phys_base + offset;

    /*
     * Translate VMO permissions into x86 page-table flags.
     */
    map_flags = PAGE_PRESENT;

    if (rights & VMO_WRITE)
        map_flags |= PAGE_WRITABLE;

    if (!(rights & VMO_EXEC))
        map_flags |= PAGE_NX;

    /*
     * Mappings in the lower canonical half are userspace mappings.
     */
    if (va < USER_CANONICAL_TOP)
        map_flags |= PAGE_USER;

    /*
     * Establish the actual page-table mapping.
     *
     * PMEM-backed VMOs do not use ADDRESS_SPACE_MAP_OWNED: destroying
     * the mapping must not implicitly free the physical memory described
     * by the PMEM object.
     */
    if (address_space_map_range(as,
                                va,
                                phys,
                                length,
                                map_flags) != 0)
        return -1;

    /*
     * Every VMO-backed mapping must have a corresponding vmoset entry.
     *
     * If the bookkeeping insertion fails, roll the page-table mapping
     * back immediately.
     */
    if (kvmoset_add(as->vmoset,
                    handle,
                    va,
                    offset,
                    length,
                    rights) != 0) {
        if (address_space_unmap_range(as, va, length) != 0)
            memory_panic("VMO map rollback failed");

        return -1;
    }

    return 0;
}

int kvmo_unmap(vmo_handle_t handle,
               address_space_t *as,
               uintptr_t va,
               size_t length)
{
    vmoset_entry_t entry;

    if (as == NULL)
        return -1;

    if (handle == VMO_INVALID_HANDLE)
        return -1;

    if (length == 0)
        return -1;

    /*
     * Find the VMO mapping containing this address.
     */
    if (kvmoset_find(as->vmoset, va, &entry) != 0)
        return -1;

    /*
     * For now, unmapping operates on one complete recorded mapping.
     *
     * Partial unmapping will require splitting vmoset entries and is
     * deliberately not supported yet.
     */
    if (entry.vmo != handle)
        return -1;

    if (entry.virtual_address != va)
        return -1;

    if (entry.length != length)
        return -1;

    /*
     * Tear down the actual page-table mapping first.
     *
     * If this fails, leave the vmoset entry intact: it still describes
     * the mapping we believe exists.
     */
    if (address_space_unmap_range(as, va, length) != 0)
        return -1;

    /*
     * The mapping no longer exists, so its bookkeeping entry must now
     * disappear as well.
     *
     * Failure here would violate the fundamental vmoset invariant and
     * isn't something the caller can sensibly recover from.
     */
    if (kvmoset_remove(as->vmoset, va) != 0)
        memory_panic("VMO unmap bookkeeping removal failed");

    return 0;
}

int kvmo_protect(vmo_handle_t handle,
                 address_space_t *as,
                 uintptr_t va,
                 size_t length,
                 vmo_rights_t rights)
{
    vmo_t vmo;
    vmoset_entry_t entry;
    uint64_t flags;

    if (as == NULL)
        return -1;

    if (handle == VMO_INVALID_HANDLE)
        return -1;

    if (length == 0)
        return -1;

    /*
     * VMO_MAP is an operation right, not a mapping permission.
     */
    if (rights & ~(VMO_READ | VMO_WRITE | VMO_EXEC))
        return -1;

    /*
     * x86-64 ordinary page tables cannot represent a present mapping
     * which is not readable.
     */
    if (!(rights & VMO_READ))
        return -1;

    if (kvmo_get(handle, &vmo) != 0)
        return -1;

    /*
     * The new mapping permissions must remain within the VMO's
     * intrinsic rights.
     */
    if (rights & ~vmo.rights)
        return -1;

    /*
     * Locate the existing mapping.
     */
    if (kvmoset_find(as->vmoset, va, &entry) != 0)
        return -1;

    /*
     * For now protect operates on one complete recorded mapping.
     * Supporting partial protection later requires splitting the
     * corresponding vmoset entry.
     */
    if (entry.vmo != handle)
        return -1;

    if (entry.virtual_address != va)
        return -1;

    if (entry.length != length)
        return -1;

    /*
     * Translate VMO permissions into page-table flags.
     */
    flags = PAGE_PRESENT;

    if (rights & VMO_WRITE)
        flags |= PAGE_WRITABLE;

    if (!(rights & VMO_EXEC))
        flags |= PAGE_NX;

    if (va < USER_CANONICAL_TOP)
        flags |= PAGE_USER;

    /*
     * Change the actual mapping first. If this fails, the vmoset still
     * correctly describes the old mapping.
     */
    if (address_space_protect_range(as, va, length, flags) != 0)
        return -1;

    /*
     * Now update the bookkeeping to describe the new permissions.
     *
     * We need a proper vmoset operation for this rather than reaching
     * into the set internals here.
     */
    if (kvmoset_set_rights(as->vmoset, va, rights) != 0)
        memory_panic("VMO protect bookkeeping update failed");

    return 0;
}

int kvmoset_unmap_all(vmoset_handle_t handle,
                      address_space_t *as)
{
    vmoset_t *set;
    vmoset_entry_t *entry;

    if (as == NULL)
        return -1;

    if (handle == VMOSET_INVALID_HANDLE)
        return -1;

    for (;;) {
        vmo_handle_t vmo;
        uintptr_t va;
        size_t length;

        kspin_lock(&vmosets_lock);

        set = vmoset_lookup_locked(handle);
        if (set == NULL) {
            kspin_unlock(&vmosets_lock);
            return -1;
        }

        kspin_lock(&set->spinlock);
        kspin_unlock(&vmosets_lock);

        entry = set->entries;

        /*
         * Empty set: all mappings have been successfully removed.
         */
        if (entry == NULL) {
            kspin_unlock(&set->spinlock);
            return 0;
        }

        /*
         * Take a snapshot of the entry we are about to remove.
         *
         * We must release the vmoset lock before calling kvmo_unmap(),
         * because kvmo_unmap() ultimately calls back into vmoset code
         * to remove the bookkeeping entry.
         */
        vmo = entry->vmo;
        va = entry->virtual_address;
        length = entry->length;

        kspin_unlock(&set->spinlock);

        if (kvmo_unmap(vmo, as, va, length) != 0)
            return -1;
    }
}
