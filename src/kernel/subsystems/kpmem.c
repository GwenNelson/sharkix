#include <string.h>

#include <sharkix/kernel/memory.h>
#include <sharkix/kernel/subsystems/kpmem.h>
#include <sharkix/kernel/sync.h>

/*
 * Registry records are private: kpmem_get() returns only a descriptive pmem_t.
 * Each registered record owns one reference. Each child owns one reference
 * to each distinct immediate parent, until the child itself reaches zero.
 * All reference counts and parent relationships use the registry mutex.
 */
typedef struct pmem_record_t {
    pmem_handle_t handle;
    uintptr_t phys_base;
    size_t length;
    bool owns_pages;
    size_t refcount;
    size_t parent_count;
    struct pmem_record_t *parents[2];
    /* Used only after the final reference disappears. */
    struct pmem_record_t *release_next;
    UT_hash_handle hh;
} pmem_record_t;

static pmem_record_t *global_pmem_table;
static pmem_handle_t next_pmem_handle;
static kmutex_t global_pmem_table_lock;

static int kpmem_get_end(uintptr_t base, size_t length, uintptr_t *end) {
	if(!end) return -1;

	if ((uintmax_t)length > (uintmax_t)UINTPTR_MAX - (uintmax_t)base) return -1;

	*end = base + (uintptr_t)length;
	return 0;
}

static pmem_record_t *kpmem_find_locked(pmem_handle_t handle)
{
    pmem_record_t *pmem = NULL;
    uint32_t hashv = (uint32_t)handle;

    HASH_FIND_BYHASHVALUE(hh, global_pmem_table, &handle, sizeof(handle), hashv, pmem);
    return pmem;
}

static int kpmem_insert_locked(pmem_record_t *pmem)
{
    if (next_pmem_handle == PMEM_INVALID_HANDLE)
        return -1;

    pmem->handle = next_pmem_handle++;
    uint32_t hashv = (uint32_t)pmem->handle;

    HASH_ADD_BYHASHVALUE(hh,
             global_pmem_table,
             handle,
             sizeof(pmem->handle),
             hashv,
             pmem);

    return 0;
}

void kpmem_init(void)
{
    global_pmem_table = NULL;
    next_pmem_handle = 1;

    kmutex_init(&global_pmem_table_lock);
}

int kpmem_get(pmem_handle_t handle, pmem_t *out) {
    pmem_record_t *pmem;

    if (!out || handle == PMEM_INVALID_HANDLE)
        return -1;

    kmutex_lock(&global_pmem_table_lock);

    pmem = kpmem_find_locked(handle);
    if (!pmem) {
        kmutex_unlock(&global_pmem_table_lock);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->handle = pmem->handle;
    out->phys_base = pmem->phys_base;
    out->length = pmem->length;
    out->owns_pages = pmem->owns_pages;

    kmutex_unlock(&global_pmem_table_lock);
    return 0;
}

static int kpmem_create_internal(pmem_handle_t *out,
                                 uintptr_t base,
                                 size_t len,
                                 bool owns_pages)
{
    pmem_record_t *pmem;
    uintptr_t end;
    int result;

    if (!out)
        return -1;

    *out = PMEM_INVALID_HANDLE;

    if (kpmem_get_end(base, len, &end) < 0)
        return -1;

    if (owns_pages &&
        (len == 0 || base % PAGE_SIZE != 0 || len % PAGE_SIZE != 0))
        return -1;

    pmem = kmalloc(sizeof(*pmem));
    if (!pmem)
        return -1;

    memset(pmem, 0, sizeof(*pmem));
    pmem->phys_base = base;
    pmem->length = len;
    pmem->owns_pages = owns_pages;
    pmem->refcount = 1;

    kmutex_lock(&global_pmem_table_lock);
    result = kpmem_insert_locked(pmem);
    if (result == 0)
        *out = pmem->handle;
    kmutex_unlock(&global_pmem_table_lock);

    if (result < 0) {
        kfree(pmem);
        return -1;
    }

    return 0;
}

int kpmem_create(pmem_handle_t *out, uintptr_t base, size_t len)
{
    return kpmem_create_internal(out, base, len, false);
}

int kpmem_alloc_owned_pages(pmem_handle_t *out,
                            size_t requested_len,
                            size_t *actual_len)
{
    uint64_t physical_base;
    size_t rounded_len;
    size_t page_count;

    if (!out || !actual_len)
        return -1;

    *out = PMEM_INVALID_HANDLE;
    *actual_len = 0;

    if (requested_len == 0 ||
        requested_len > SIZE_MAX - (PAGE_SIZE - 1))
        return -1;

    rounded_len = (requested_len + PAGE_SIZE - 1) &
                  ~(size_t)(PAGE_SIZE - 1);
    page_count = rounded_len / PAGE_SIZE;

    if (!phys_alloc_pages(page_count, &physical_base)) return -1;

    memset(phys_to_virt(physical_base), 0, rounded_len);

    if (kpmem_create_internal(out,
                              (uintptr_t)physical_base,
                              rounded_len,
                              true) != 0) {
        phys_free_pages(physical_base, page_count);
        return -1;
    }

    *actual_len = rounded_len;
    return 0;
}

int kpmem_derive(pmem_handle_t source,
                  uintptr_t new_base,
                  size_t new_len,
                  pmem_handle_t *out)
{
    pmem_record_t *source_pmem;
    pmem_record_t *derived;
    uintptr_t source_end;
    uintptr_t new_end;
    int result;

    if (!out)
        return -1;

    *out = PMEM_INVALID_HANDLE;

    if (kpmem_get_end(new_base, new_len, &new_end) < 0)
        return -1;

    derived = kmalloc(sizeof(*derived));
    if (!derived)
        return -1;

    memset(derived, 0, sizeof(*derived));
    derived->owns_pages = false;
    derived->refcount = 1;

    kmutex_lock(&global_pmem_table_lock);

    source_pmem = kpmem_find_locked(source);
    if (!source_pmem ||
        kpmem_get_end(source_pmem->phys_base, source_pmem->length, &source_end) < 0 ||
        new_base < source_pmem->phys_base || new_end > source_end ||
        source_pmem->refcount == SIZE_MAX) {
        kmutex_unlock(&global_pmem_table_lock);
        kfree(derived);
        return -1;
    }

    derived->phys_base = new_base;
    derived->length = new_len;
    /* Retain before publishing; the source cannot disappear under this lock. */
    ++source_pmem->refcount;
    derived->parents[0] = source_pmem;
    derived->parent_count = 1;
    result = kpmem_insert_locked(derived);
    if (result == 0)
        *out = derived->handle;
    else
        --source_pmem->refcount; /* Its registry reference still exists. */

    kmutex_unlock(&global_pmem_table_lock);

    if (result < 0) {
        kfree(derived);
        return -1;
    }

    return 0;
}

int kpmem_merge(pmem_handle_t a, pmem_handle_t b, pmem_handle_t *out)
{
    pmem_record_t *pmem_a;
    pmem_record_t *pmem_b;
    pmem_record_t *merged;
    pmem_record_t *lo;
    pmem_record_t *hi;
    uintptr_t a_end;
    uintptr_t b_end;
    uintptr_t lo_end;
    uintptr_t hi_end;
    uintptr_t new_base;
    uintptr_t new_end;
    int result;

    if (!out)
        return -1;

    *out = PMEM_INVALID_HANDLE;

    merged = kmalloc(sizeof(*merged));
    if (!merged)
        return -1;

    memset(merged, 0, sizeof(*merged));
    merged->owns_pages = false;
    merged->refcount = 1;

    kmutex_lock(&global_pmem_table_lock);

    pmem_a = kpmem_find_locked(a);
    pmem_b = kpmem_find_locked(b);

    if (!pmem_a || !pmem_b ||
        kpmem_get_end(pmem_a->phys_base, pmem_a->length, &a_end) < 0 ||
        kpmem_get_end(pmem_b->phys_base, pmem_b->length, &b_end) < 0) {
        kmutex_unlock(&global_pmem_table_lock);
        kfree(merged);
        return -1;
    }

    if (pmem_a->length == 0 && pmem_b->length == 0) {
        merged->phys_base = pmem_a->phys_base < pmem_b->phys_base ?
                            pmem_a->phys_base : pmem_b->phys_base;
        merged->length = 0;
    } else if (pmem_a->length == 0) {
        merged->phys_base = pmem_b->phys_base;
        merged->length = pmem_b->length;
    } else if (pmem_b->length == 0) {
        merged->phys_base = pmem_a->phys_base;
        merged->length = pmem_a->length;
    } else {
        if (pmem_a->phys_base <= pmem_b->phys_base) {
            lo = pmem_a;
            lo_end = a_end;
            hi = pmem_b;
            hi_end = b_end;
        } else {
            lo = pmem_b;
            lo_end = b_end;
            hi = pmem_a;
            hi_end = a_end;
        }

        if (hi->phys_base > lo_end) {
            kmutex_unlock(&global_pmem_table_lock);
            kfree(merged);
            return -1;
        }

        new_base = lo->phys_base;
        new_end = lo_end > hi_end ? lo_end : hi_end;

        if ((uintmax_t)(new_end - new_base) > (uintmax_t)SIZE_MAX) {
            kmutex_unlock(&global_pmem_table_lock);
            kfree(merged);
            return -1;
        }

        merged->phys_base = new_base;
        merged->length = (size_t)(new_end - new_base);
    }

    /* Check every counter before acquiring any references. */
    if (pmem_a->refcount == SIZE_MAX || pmem_b->refcount == SIZE_MAX) {
        kmutex_unlock(&global_pmem_table_lock);
        kfree(merged);
        return -1;
    }

    merged->parents[0] = pmem_a;
    merged->parent_count = 1;
    ++pmem_a->refcount;
    if (pmem_b != pmem_a) {
        merged->parents[1] = pmem_b;
        merged->parent_count = 2;
        ++pmem_b->refcount;
    }

    result = kpmem_insert_locked(merged);
    if (result == 0) {
        *out = merged->handle;
    } else {
        /* Both sources still have their registry references under this lock. */
        for (size_t i = 0; i < merged->parent_count; ++i)
            --merged->parents[i]->refcount;
    }

    kmutex_unlock(&global_pmem_table_lock);

    if (result < 0) {
        kfree(merged);
        return -1;
    }

    return 0;
}

/*
 * Drop one reference and enqueue the record only on the transition to zero.
 * The caller holds the registry mutex. Zero-count records are no longer
 * registered and cannot acquire new references.
 */
static void kpmem_release_locked(pmem_record_t *pmem, pmem_record_t **pending)
{
    if (pmem->refcount == 0)
        memory_panic("PMEM reference underflow");

    if (--pmem->refcount == 0) {
        pmem->release_next = *pending;
        *pending = pmem;
    }
}

int kpmem_destroy(pmem_handle_t handle)
{
    pmem_record_t *pmem;
    pmem_record_t *pending = NULL;
    pmem_record_t *dead = NULL;

    kmutex_lock(&global_pmem_table_lock);

    pmem = kpmem_find_locked(handle);
    if (!pmem) {
        kmutex_unlock(&global_pmem_table_lock);
        return -1;
    }

    /* Stale handles fail immediately, even if children retain the record. */
    HASH_DEL(global_pmem_table, pmem);
    kpmem_release_locked(pmem, &pending);

    /*
     * Drain the parent graph without recursion or temporary allocations.
     * A child releases its parents only at final reclamation, so destroying
     * an intermediate handle leaves its descendants' backing alive.
     * Shared ancestors reach zero only after all incoming references go away.
     */
    while (pending) {
        pmem = pending;
        pending = pmem->release_next;
        for (size_t i = 0; i < pmem->parent_count; ++i)
            kpmem_release_locked(pmem->parents[i], &pending);
        pmem->release_next = dead;
        dead = pmem;
    }

    kmutex_unlock(&global_pmem_table_lock);

    /* No live record can reach this list. Allocator work needs no PMEM lock. */
    while (dead) {
        pmem = dead;
        dead = pmem->release_next;
        if (pmem->owns_pages)
            phys_free_pages((uint64_t)pmem->phys_base, pmem->length / PAGE_SIZE);
        kfree(pmem);
    }
    return 0;
}
