#pragma once

// Wrapper around the generic uthash.h for use inside the kernel

#include <sharkix/kernel/memory.h>
#include <sharkix/kernel/console.h>
#include <string.h>

#define uthash_malloc(sz)    kmalloc(sz)
#define uthash_free(ptr, sz) kfree(ptr)
#define uthash_bzero(a, n)   memset((a), 0, (n))
#define uthash_strlen(s)     strlen(s)
#define uthash_fatal(msg)    do { \
	console_write("UTHASH FATAL: "); \
	console_write(msg); \
	console_write("\n"); \
	for(;;) __asm__ volatile ("cli; hlt"); \
} while(0)

#include <uthash.h>
