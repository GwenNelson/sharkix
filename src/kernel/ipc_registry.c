#include <sharkix/kernel/uthash.h>
#include <sharkix/kernel/ipc.h>

#include <sharkix/kernel/memory.h>
#include <sharkix/kernel/sync.h>

#include <sharkix/kernel/ipc_registry.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct kipc_registry_entry_t {
	char name[IPC_REGNAME_MAX + 1];
	size_t name_len;
	ipc_handle_t endpoint;
	UT_hash_handle hh;
} kipc_registry_entry_t;

static kipc_registry_entry_t *registry_table = NULL;
static kmutex_t registry_table_lock;

void kipc_registry_init(void) {
     registry_table = NULL;
     kmutex_init(&registry_table_lock);
}

int kipc_registry_lookup(const char* name, ipc_handle_t *out) {
    kipc_registry_entry_t* entry = NULL;
    if (!name || !out) return -1;
    if (strlen(name) > IPC_REGNAME_MAX) return -1;

    kmutex_lock(&registry_table_lock);
    HASH_FIND_STR(registry_table, name, entry);
    if (entry) *out = entry->endpoint;
    kmutex_unlock(&registry_table_lock);

    return entry ? 0 : -1;
}

int kipc_registry_unregister(const char* name) {
    kipc_registry_entry_t* entry = NULL;
    if (!name) return -1;
    if (strlen(name) > IPC_REGNAME_MAX) return -1;

    kmutex_lock(&registry_table_lock);
    HASH_FIND_STR(registry_table, name, entry);
    if (entry) HASH_DEL(registry_table, entry);
    kmutex_unlock(&registry_table_lock);

    if (!entry) return -1;
    kfree(entry);
    return 0;
}

int kipc_registry_register(const char* name, ipc_handle_t endpoint) {
    if (!name) return -1;
    size_t len = strlen(name);
    if (len > IPC_REGNAME_MAX) return -1;

    kipc_registry_entry_t* entry = NULL;
    entry = kmalloc(sizeof(*entry));
    if(!entry) return -1;
    memset(entry,0,sizeof(*entry));
    entry->endpoint = endpoint;
    entry->name_len = len;
    memcpy(entry->name, name, len + 1);

    kmutex_lock(&registry_table_lock);
    kipc_registry_entry_t* existing = NULL;
    HASH_FIND_STR(registry_table, name, existing);
    if (existing) {
        kmutex_unlock(&registry_table_lock);
        kfree(entry);
        return -1;
    }
    HASH_ADD_STR(registry_table, name, entry);
    kmutex_unlock(&registry_table_lock);
    return 0;
}
