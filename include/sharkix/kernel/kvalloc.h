#pragma once

#include <stddef.h>
#include <stdint.h>

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
