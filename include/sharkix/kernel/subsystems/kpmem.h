#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sharkix/kernel/uthash.h>

typedef uint64_t pmem_handle_t;

#define PMEM_INVALID_HANDLE ((pmem_handle_t)UINT64_MAX)

/*
 * PMEM_ALLOC operation flags.
 *
 * The least significant bit selects whether the syscall allocates fresh,
 * anonymous pages or describes an arbitrary physical range.  No other flag
 * bits are currently valid.
 */
#define KPMEM_ALLOC_FLAG_ANON   UINT64_C(0)
#define KPMEM_ALLOC_FLAG_ANY    (UINT64_C(1) << 0)
#define KPMEM_ALLOC_VALID_FLAGS KPMEM_ALLOC_FLAG_ANY

// represents a region of contiguous physical memory with no holes
typedef struct pmem_t {
	pmem_handle_t handle;
	uintptr_t     phys_base;
	size_t        length;
	bool          owns_pages;

	UT_hash_handle hh;
} pmem_t;

void kpmem_init(void);

int kpmem_get(pmem_handle_t handle, pmem_t *out);

// create a new physical memory object, returns 0 on success and -1 on failure
int kpmem_create(pmem_handle_t *out, uintptr_t base, size_t len);

/*
 * Allocate fresh, zero-filled physical pages and create a PMEM which owns
 * them.  The requested length is rounded up to a whole number of pages and
 * returned through actual_len.
 */
int kpmem_alloc_owned_pages(pmem_handle_t *out,
                            size_t requested_len,
                            size_t *actual_len);

// derive a new sub-region, returns 0 on success and -1 on failure
int kpmem_derive(pmem_handle_t source, uintptr_t new_base, size_t new_len, pmem_handle_t* out);

// merge two regions (a,b) into a new region (*out), non-destructively
// there must be no "holes"
// this is NOT ordered, either of (a,b) can have a lower address - so long as the union covers an entire
// contiguous region
int kpmem_merge(pmem_handle_t a, pmem_handle_t b, pmem_handle_t *out);

// delete a physical memory object - obviously this doesn't somehow erase physical memory
// that'd require like a robot that yanks DIMMs out, and that belongs in userspace
// implementing DIMM-yanking robots is out of scope for the kernel
//
// but seriously, it returns -1 on failure and frees the pmem_t structure.
// If the object owns its physical pages, those pages are released as well.
int kpmem_destroy(pmem_handle_t handle);
