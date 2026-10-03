#include <string.h>

#include <sharkix/kernel/memory.h>
#include <sharkix/kernel/subsystems/kpmem.h>
#include <sharkix/kernel/sync.h>

static pmem_t *global_pmem_table;
static pmem_handle_t next_pmem_handle;
static kmutex_t global_pmem_table_lock;

static int kpmem_get_end(uintptr_t base, size_t length, uintptr_t *end) {
	if(!end) return -1;

	if ((uintmax_t)length > (uintmax_t)UINTPTR_MAX - (uintmax_t)base) return -1;

	*end = base + (uintptr_t)length;
	return 0;
}

static pmem_t *kpmem_find_locked(pmem_handle_t handle)
{
    pmem_t *pmem = NULL;
    uint32_t hashv = (uint32_t)handle;

    HASH_FIND_BYHASHVALUE(hh, global_pmem_table, &handle, sizeof(handle), hashv, pmem);
    return pmem;
}

static int kpmem_insert_locked(pmem_t *pmem)
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
    pmem_t *pmem;

    if (!out || handle == PMEM_INVALID_HANDLE)
        return -1;

    kmutex_lock(&global_pmem_table_lock);

    pmem = kpmem_find_locked(handle);
    if (!pmem) {
        kmutex_unlock(&global_pmem_table_lock);
        return -1;
    }

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
    pmem_t *pmem;
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

    kmutex_lock(&global_pmem_table_lock);
    result = kpmem_insert_locked(pmem);
    kmutex_unlock(&global_pmem_table_lock);

    if (result < 0) {
        kfree(pmem);
        return -1;
    }

    *out = pmem->handle;
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

    if (phys_alloc_pages(page_count, &physical_base) != 0) return -1;

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
    pmem_t *source_pmem;
    pmem_t *derived;
    uintptr_t source_end;
    uintptr_t new_end;
    int result;

    if (!out)
        return -1;

    if (kpmem_get_end(new_base, new_len, &new_end) < 0)
        return -1;

    derived = kmalloc(sizeof(*derived));
    if (!derived)
        return -1;

    memset(derived, 0, sizeof(*derived));
    derived->owns_pages = false;

    kmutex_lock(&global_pmem_table_lock);

    source_pmem = kpmem_find_locked(source);
    if (!source_pmem ||
        kpmem_get_end(source_pmem->phys_base, source_pmem->length, &source_end) < 0 ||
        new_base < source_pmem->phys_base || new_end > source_end) {
        kmutex_unlock(&global_pmem_table_lock);
        kfree(derived);
        return -1;
    }

    derived->phys_base = new_base;
    derived->length = new_len;
    result = kpmem_insert_locked(derived);

    kmutex_unlock(&global_pmem_table_lock);

    if (result < 0) {
        kfree(derived);
        return -1;
    }

    *out = derived->handle;
    return 0;
}

int kpmem_merge(pmem_handle_t a, pmem_handle_t b, pmem_handle_t *out)
{
    pmem_t *pmem_a;
    pmem_t *pmem_b;
    pmem_t *merged;
    pmem_t *lo;
    pmem_t *hi;
    uintptr_t a_end;
    uintptr_t b_end;
    uintptr_t lo_end;
    uintptr_t hi_end;
    uintptr_t new_base;
    uintptr_t new_end;
    int result;

    if (!out)
        return -1;

    merged = kmalloc(sizeof(*merged));
    if (!merged)
        return -1;

    memset(merged, 0, sizeof(*merged));
    merged->owns_pages = false;

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

    result = kpmem_insert_locked(merged);

    kmutex_unlock(&global_pmem_table_lock);

    if (result < 0) {
        kfree(merged);
        return -1;
    }

    *out = merged->handle;
    return 0;
}

int kpmem_destroy(pmem_handle_t handle)
{
    pmem_t *pmem;
    uintptr_t phys_base;
    size_t page_count;
    bool owns_pages;

    kmutex_lock(&global_pmem_table_lock);

    pmem = kpmem_find_locked(handle);
    if (!pmem) {
        kmutex_unlock(&global_pmem_table_lock);
        return -1;
    }

    HASH_DEL(global_pmem_table, pmem);

    phys_base = pmem->phys_base;
    page_count = pmem->length / PAGE_SIZE;
    owns_pages = pmem->owns_pages;

    kmutex_unlock(&global_pmem_table_lock);

    if (owns_pages)
        phys_free_pages((uint64_t)phys_base, page_count);

    kfree(pmem);
    return 0;
}
