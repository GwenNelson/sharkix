#pragma once

#include <stddef.h>
#include <stdint.h>

#include <sharkix/kernel/vmo.h>
#include <sharkix/kernel/memory.h>
#include <sharkix/kernel/sync.h>
#include <sharkix/kernel/uthash.h>

/*
 * NO ONE HEARS YOU, YOUR KERNEL IS FRACTURED
 * (I was listening to Slayer when i wrote this, so what?)
 *
 * Anyway, this module is basically the kobject-ish thin wrapper over the existing address_space_t stuff
 * It is purposefully VERY slim, use the memory.h functions - this is just a thin wrapper so userspace
 * and caps etc all work with address_space_t*
 *
 */

typedef uint64_t as_handle_t;

#define AS_INVALID_HANDLE    ((as_handle_t)UINT64_MAX)

typedef struct as_t {
	as_handle_t      handle;
	address_space_t* address_space;
	UT_hash_handle hh;
} as_t;

// CAN'T STOP THE INIT, I LOVE IT
// this function initializes the new address-space subsystem
// IT'S MY MICROKERNEL, FUCK OFF
void kas_init(void);

// professionalism somewhat restored from this line onward

// creates a new address space HANDLE, from an existing address_space_t*
int kas_register(as_handle_t* out, address_space_t* as);

// looks up an address space and retains it until kas_release is called
int kas_acquire(as_handle_t handle, address_space_t** out);

// drops a reference acquired with kas_acquire
void kas_release(address_space_t* address_space);

// destroy an address space HANDLE, to destroy the actual address space, use the functions in memory.h
int kas_unregister(as_handle_t handle);
