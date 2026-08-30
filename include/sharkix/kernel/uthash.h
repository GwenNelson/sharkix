#pragma once

// Wrapper around the generic uthash.h for use inside the kernel

#include <sharkix/kernel/memory.h>
#include <string.h>

#define uthash_malloc(sz)    kmalloc(sz)
#define uthash_free(ptr, sz) kfree(ptr)
#define uthash_bzero(a, n)   memset((a), 0, (n))
#define uthash_strlen(s)     strlen(s)

#include <uthash.h>
