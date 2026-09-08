/* src/kernel/kvalloc.c */

#include <stddef.h>
#include <stdint.h>

#include <sharkix/kernel/kvalloc.h>
#include <sharkix/kernel/memory.h>
#include <sharkix/kernel/sync.h>

#define KVALLOC_BASE UINT64_C(0xfffff00000000000)
#define KVALLOC_SIZE UINT64_C(0x0000008000000000)
#define KVALLOC_END  (KVALLOC_BASE + KVALLOC_SIZE)

typedef struct kvalloc_region {
    uintptr_t base;
    size_t length;

    struct kvalloc_region *next;
} kvalloc_region_t;

static kvalloc_region_t *regions;
static kmutex_t kvalloc_mutex;
static int kvalloc_initialized;

static int round_up_page(size_t size, size_t *out) {
    size_t mask = PAGE_SIZE - 1;

    if (size == 0)
        return -1;

    if (size > SIZE_MAX - mask)
        return -1;

    *out = (size + mask) & ~mask;
    return 0;
}

int kvalloc_init(void) {
    regions = NULL;

    kmutex_init(&kvalloc_mutex);

    kvalloc_initialized = 1;
    return 0;
}

int kvalloc(size_t size, uintptr_t *out) {
    kvalloc_region_t *entry;
    kvalloc_region_t *prev;
    kvalloc_region_t *cur;
    uintptr_t candidate;
    size_t length;

    if (!out)
        return -1;

    if (!kvalloc_initialized)
        return -1;

    if (round_up_page(size, &length) != 0)
        return -1;

    entry = kmalloc(sizeof(*entry));
    if (!entry)
        return -1;

    kmutex_lock(&kvalloc_mutex);

    candidate = KVALLOC_BASE;
    prev = NULL;
    cur = regions;

    while (cur) {
        /*
         * Is the gap before `cur` large enough?
         *
         * Avoid candidate + length overflow by subtracting instead.
         */
        if (candidate <= cur->base &&
            length <= (size_t)(cur->base - candidate)) {
            break;
        }

        /*
         * Otherwise, continue searching immediately after this region.
         */
        if (cur->base > UINTPTR_MAX - cur->length) {
            kmutex_unlock(&kvalloc_mutex);
            kfree(entry);
            return -1;
        }

        candidate = cur->base + cur->length;

        prev = cur;
        cur = cur->next;
    }

    /*
     * Check that the requested range fits within the kvalloc arena.
     */
    if (candidate > KVALLOC_END ||
        length > (size_t)(KVALLOC_END - candidate)) {
        kmutex_unlock(&kvalloc_mutex);
        kfree(entry);
        return -1;
    }

    entry->base = candidate;
    entry->length = length;
    entry->next = cur;

    if (prev)
        prev->next = entry;
    else
        regions = entry;

    kmutex_unlock(&kvalloc_mutex);

    *out = candidate;
    return 0;
}

int kvfree(uintptr_t addr) {
    kvalloc_region_t *prev;
    kvalloc_region_t *cur;

    if (!kvalloc_initialized)
        return -1;

    kmutex_lock(&kvalloc_mutex);

    prev = NULL;
    cur = regions;

    while (cur) {
        if (cur->base == addr)
            break;

        if (cur->base > addr) {
            kmutex_unlock(&kvalloc_mutex);
            return -1;
        }

        prev = cur;
        cur = cur->next;
    }

    if (!cur) {
        kmutex_unlock(&kvalloc_mutex);
        return -1;
    }

    if (prev)
        prev->next = cur->next;
    else
        regions = cur->next;

    kmutex_unlock(&kvalloc_mutex);

    kfree(cur);
    return 0;
}
