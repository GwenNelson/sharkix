#pragma once

#include <stddef.h>
#include <stdint.h>

#include <sharkix/kernel/pmem.h>
#include <sharkix/kernel/sync.h>
#include <sharkix/kernel/uthash.h>

/*
 * Forward declaration to avoid a circular include with memory.h.
 */
typedef struct address_space address_space_t;


/*
 * Handles and rights
 */

typedef uint64_t vmo_handle_t;
typedef uint64_t vmoset_handle_t;
typedef uint64_t vmo_rights_t;

#define VMO_INVALID_HANDLE    ((vmo_handle_t)UINT64_MAX)
#define VMOSET_INVALID_HANDLE ((vmoset_handle_t)UINT64_MAX)


/*
 * Rights intrinsic to a VMO.
 *
 * These are NOT capability rights.
 *
 * They describe the maximum operations and mapping permissions supported
 * by the VMO itself. A capability referring to the VMO may further
 * restrict these rights.
 */
#define VMO_NONE          UINT64_C(0)
#define VMO_MAP          (UINT64_C(1) << 0)
#define VMO_READ         (UINT64_C(1) << 1)
#define VMO_WRITE        (UINT64_C(1) << 2)
#define VMO_EXEC         (UINT64_C(1) << 3)

#define VMO_VALID_RIGHTS (VMO_MAP   | \
                          VMO_READ  | \
                          VMO_WRITE | \
                          VMO_EXEC)



typedef enum vm_status_t {
#define SHARKIX_ERRNO(name,value,msg) name = value,
#include <sharkix/kernel/vm_errno.inc>
#undef SHARKIX_ERRNO
} vm_status_t;



/*
 * Represents one virtual memory object.
 *
 * For now every VMO is backed by exactly one contiguous PMEM object.
 *
 * The VMO subsystem does not perform capability checks and does not
 * determine whether its kernel caller was authorised to create a VMO
 * with a particular set of rights. That belongs to the syscall/capability
 * layer.
 *
 * Future VMO implementations may use other forms of backing.
 */
typedef struct vmo_t {
    vmo_handle_t  handle;
    vmo_rights_t  rights;

    /* PMEM backing for the current implementation. */
    pmem_handle_t pmem;

    UT_hash_handle hh;
} vmo_t;


/*
 * Represents one mapping of a VMO into an address space.
 *
 * The same VMO may be mapped into an address space more than once, and
 * may also be mapped into multiple different address spaces.
 *
 * A vmoset entry represents one such mapping relationship.
 */
typedef struct vmoset_entry_t {
    uintptr_t     virtual_address;
    size_t        length;

    vmo_handle_t  vmo;
    size_t        offset;

    /*
     * Current mapping permissions.
     *
     * VMO_MAP is not a page permission and should not appear here.
     * This is expected to contain only VMO_READ, VMO_WRITE and VMO_EXEC.
     */
    vmo_rights_t  rights;

    UT_hash_handle hh;
} vmoset_entry_t;


/*
 * Represents the set of VMO mappings associated with one address space.
 *
 * A vmoset is bookkeeping for VMO-backed mappings. It does not itself own
 * the VMO objects referenced by its entries.
 */
typedef struct vmoset_t {
    vmoset_handle_t handle;
    vmoset_entry_t *entries;

    kspinlock_t      spinlock;
    UT_hash_handle   hh;
} vmoset_t;


/*
 * Initialise the global VMO and VMO-set tables.
 */
void kvmo_init(void);


/*
 * Create a new VMO backed by an existing PMEM object.
 *
 * The VMO subsystem trusts its kernel caller to have established that
 * 'rights' are appropriate for the supplied PMEM backing.
 *
 * In particular, this function performs no capability checks and does not
 * compare these rights against any PMEM capability.
 *
 * Returns 0 on success and -1 on failure.
 */
int kvmo_create(vmo_handle_t *out,
                pmem_handle_t pmem,
                vmo_rights_t rights);


/*
 * Get a copy of a VMO's descriptive data.
 *
 * Returns 0 on success and -1 on failure.
 */
int kvmo_get(vmo_handle_t handle,
             vmo_t *out);


/*
 * Map part of a VMO into an address space.
 *
 * 'offset' and 'length' describe the region within the VMO.
 *
 * 'rights' describes the requested mapping permissions and may contain
 * only VMO_READ, VMO_WRITE and VMO_EXEC.
 *
 * The requested mapping permissions must not exceed the intrinsic rights
 * of the VMO, and the VMO itself must possess VMO_MAP.
 *
 * On success, the corresponding mapping is also recorded in the
 * address space's vmoset.
 *
 * Returns 0 on success and -1 on failure.
 */
int kvmo_map(vmo_handle_t handle,
             address_space_t *as,
             uintptr_t va,
             size_t offset,
             size_t length,
             vmo_rights_t rights);


/*
 * Remove a VMO mapping from an address space.
 *
 * This removes the underlying address-space mapping and removes the
 * corresponding entry from the address space's vmoset.
 *
 * It does NOT destroy the VMO object.
 *
 * Returns 0 on success and -1 on failure.
 */
int kvmo_unmap(vmo_handle_t handle,
               address_space_t *as,
               uintptr_t va,
               size_t length);


/*
 * Change the permissions of an existing VMO mapping.
 *
 * 'rights' describes the new mapping permissions and may contain only
 * VMO_READ, VMO_WRITE and VMO_EXEC.
 *
 * The requested permissions must not exceed the intrinsic rights of
 * the VMO.
 *
 * On success, both the address-space mapping and the corresponding
 * vmoset entry are updated.
 *
 * Returns 0 on success and -1 on failure.
 */
int kvmo_protect(vmo_handle_t handle,
                 address_space_t *as,
                 uintptr_t va,
                 size_t length,
                 vmo_rights_t rights);


/*
 * Delete a VMO object.
 *
 * This destroys only the VMO object itself.
 *
 * It does NOT destroy its PMEM backing and does NOT automatically unmap
 * mappings of the VMO from any address space.
 *
 * Callers must ensure that any live mappings have been dealt with before
 * destroying the VMO.
 *
 * Returns 0 on success and -1 on failure.
 */
int kvmo_destroy(vmo_handle_t handle);


/*
 * Create a new empty VMO set.
 *
 * Returns 0 on success and -1 on failure.
 */
int kvmoset_new(vmoset_handle_t *out);


/*
 * Destroy a VMO set and all of its bookkeeping entries.
 *
 * This does NOT unmap any mappings represented by the set.
 * This does NOT destroy any VMO objects referenced by the set.
 *
 * An address-space teardown path should first call kvmoset_unmap_all(),
 * then destroy the now-empty set with kvmoset_destroy().
 *
 * Returns 0 on success and -1 on failure.
 */
int kvmoset_destroy(vmoset_handle_t set);


/*
 * Add a mapping record to a VMO set.
 *
 * This function modifies only VMO-set bookkeeping. It does NOT establish
 * any page-table mapping itself.
 *
 * Returns 0 on success and -1 on failure.
 */
int kvmoset_add(vmoset_handle_t set,
                vmo_handle_t vmo,
                uintptr_t va,
                size_t offset,
                size_t length,
                vmo_rights_t rights);


/*
 * Remove a mapping record from a VMO set.
 *
 * This function modifies only VMO-set bookkeeping. It does NOT unmap any
 * page-table mapping itself.
 *
 * Returns 0 on success and -1 on failure.
 */
int kvmoset_remove(vmoset_handle_t set,
                   uintptr_t va);


/*
 * Find the mapping record containing or beginning at the supplied virtual
 * address.
 *
 * The exact lookup semantics can remain implementation-defined for now;
 * callers receive a copy of the matching entry.
 *
 * Returns 0 on success and -1 on failure.
 */
int kvmoset_find(vmoset_handle_t set,
                 uintptr_t va,
                 vmoset_entry_t *out);


/*
 * Unmap every VMO mapping represented by a VMO set from the supplied
 * address space.
 *
 * Each mapping is torn down through the VMO subsystem, and its bookkeeping
 * entry is removed from the set.
 *
 * On successful completion, the VMO set remains valid but is empty.
 *
 * This does NOT destroy the VMO set itself.
 * This does NOT destroy any VMO objects referenced by the set.
 *
 * Intended primarily for clean address-space teardown.
 *
 * Returns 0 on success and -1 on failure.
 */
int kvmoset_unmap_all(vmoset_handle_t set,
                      address_space_t *as);
