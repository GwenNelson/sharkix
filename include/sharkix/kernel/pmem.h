#pragma once

#include <stddef.h>
#include <stdint.h>

#include <sharkix/kernel/uthash.h>

typedef uint64_t pmem_handle_t;

#define PMEM_INVALID_HANDLE ((pmem_handle_t)UINT64_MAX)

// represents a region of contiguous physical memory with no holes
typedef struct pmem_t {
	pmem_handle_t handle;
	uintptr_t     phys_base;
	size_t        length;

	UT_hash_handle hh;
} pmem_t;

void kpmem_init(void);

int kpmem_get(pmem_handle_t handle, pmem_t *out);

// create a new physical memory object, returns 0 on success and -1 on failure
int kpmem_create(pmem_handle_t *out, uintptr_t base, size_t len);

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
// but seriously, it returns -1 on failure, and only frees the pmem_t* structure
int kpmem_destroy(pmem_handle_t handle);
