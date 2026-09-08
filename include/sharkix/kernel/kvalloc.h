#pragma once

#include <stddef.h>
#include <stdint.h>


#define KVALLOC_BASE UINT64_C(0xfffff00000000000)
#define KVALLOC_SIZE UINT64_C(0x0000008000000000)
#define KVALLOC_END  (KVALLOC_BASE + KVALLOC_SIZE)

/*
 * Setup the kvalloc subsystem
 *
 * If this fails, it returns -1, but it should probably never fail or we're fucked
 *
 */
int kvalloc_init(void);

/*
 * Reserve a page-aligned range of kernel virtual address space.
 *
 * `size` is specified in bytes and rounded up internally to PAGE_SIZE.
 *
 * This does NOT allocate physical memory and does NOT create mappings.
 * The returned address must not be dereferenced until the caller
 * explicitly maps something there.
 *
 * Returns 0 on success, -1 on failure.
 * *out is left unchanged on failure.
 */
int kvalloc(size_t size, uintptr_t *out);

/*
 * Release a range previously reserved by kvalloc().
 *
 * This does NOT unmap anything. The caller is responsible for ensuring
 * that the range is no longer mapped before releasing it.
 *
 * Returns 0 on success, -1 if `addr` is not the base address of a live
 * kvalloc reservation.
 */
int kvfree(uintptr_t addr);
