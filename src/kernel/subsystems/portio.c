#include <string.h>

#include <sharkix/kernel/memory.h>
#include <sharkix/kernel/portio.h>
#include <sharkix/kernel/sync.h>

static portio_t *global_portio_table;
static portio_handle_t next_portio_handle;
static kmutex_t global_portio_table_lock;

static portio_t *kportio_find_locked(portio_handle_t handle)
{
    portio_t *portio = NULL;
    uint32_t hashv = (uint32_t)handle;

    HASH_FIND_BYHASHVALUE(hh, global_portio_table, &handle, sizeof(handle), hashv, portio);
    return portio;
}

static int kportio_insert_locked(portio_t *portio)
{
    if (next_portio_handle == PORTIO_INVALID_HANDLE)
        return -1;

    portio->handle = next_portio_handle++;
    uint32_t hashv = (uint32_t)portio->handle;

    HASH_ADD_BYHASHVALUE(hh,
             global_portio_table,
             handle,
             sizeof(portio->handle),
             hashv,
             portio);

    return 0;
}

static int kportio_resolve_port_locked(portio_handle_t handle,
                                       uint32_t offset,
                                       uint32_t width,
                                       uint16_t *port)
{
    portio_t *portio = kportio_find_locked(handle);
    uint32_t absolute_port;

    if (!portio || offset > portio->length ||
        width > portio->length - offset)
        return -1;

    absolute_port = (uint32_t)portio->base + offset;
    *port = (uint16_t)absolute_port;
    return 0;
}

void kportio_init(void)
{
    global_portio_table = NULL;
    next_portio_handle = 1;

    kmutex_init(&global_portio_table_lock);
}

int kportio_create(portio_handle_t *out, uint16_t base, uint32_t length)
{
    portio_t *portio;
    int result;

    if (!out || length == 0 || length > UINT32_C(0x10000) - (uint32_t)base)
        return -1;

    portio = kmalloc(sizeof(*portio));
    if (!portio)
        return -1;

    memset(portio, 0, sizeof(*portio));
    portio->base = base;
    portio->length = length;

    kmutex_lock(&global_portio_table_lock);
    result = kportio_insert_locked(portio);
    kmutex_unlock(&global_portio_table_lock);

    if (result < 0) {
        kfree(portio);
        return -1;
    }

    *out = portio->handle;
    return 0;
}

int kportio_get(portio_handle_t handle, portio_t *out)
{
    portio_t *portio;

    if (!out || handle == PORTIO_INVALID_HANDLE)
        return -1;

    kmutex_lock(&global_portio_table_lock);

    portio = kportio_find_locked(handle);
    if (!portio) {
        kmutex_unlock(&global_portio_table_lock);
        return -1;
    }

    out->handle = portio->handle;
    out->base = portio->base;
    out->length = portio->length;

    kmutex_unlock(&global_portio_table_lock);
    return 0;
}

int kportio_destroy(portio_handle_t handle)
{
    portio_t *portio;

    if (handle == PORTIO_INVALID_HANDLE)
        return -1;

    kmutex_lock(&global_portio_table_lock);

    portio = kportio_find_locked(handle);
    if (!portio) {
        kmutex_unlock(&global_portio_table_lock);
        return -1;
    }

    HASH_DEL(global_portio_table, portio);

    kmutex_unlock(&global_portio_table_lock);

    kfree(portio);
    return 0;
}

int kportio_inb(portio_handle_t handle, uint32_t offset, uint8_t *value)
{
    uint16_t port;

    if (!value)
        return -1;

    kmutex_lock(&global_portio_table_lock);
    if (kportio_resolve_port_locked(handle, offset, sizeof(*value), &port) != 0) {
        kmutex_unlock(&global_portio_table_lock);
        return -1;
    }

    __asm__ volatile ("inb %1, %0" : "=a"(*value) : "Nd"(port));

    kmutex_unlock(&global_portio_table_lock);
    return 0;
}

int kportio_inw(portio_handle_t handle, uint32_t offset, uint16_t *value)
{
    uint16_t port;

    if (!value)
        return -1;

    kmutex_lock(&global_portio_table_lock);
    if (kportio_resolve_port_locked(handle, offset, sizeof(*value), &port) != 0) {
        kmutex_unlock(&global_portio_table_lock);
        return -1;
    }

    __asm__ volatile ("inw %1, %0" : "=a"(*value) : "Nd"(port));

    kmutex_unlock(&global_portio_table_lock);
    return 0;
}

int kportio_inl(portio_handle_t handle, uint32_t offset, uint32_t *value)
{
    uint16_t port;

    if (!value)
        return -1;

    kmutex_lock(&global_portio_table_lock);
    if (kportio_resolve_port_locked(handle, offset, sizeof(*value), &port) != 0) {
        kmutex_unlock(&global_portio_table_lock);
        return -1;
    }

    __asm__ volatile ("inl %1, %0" : "=a"(*value) : "Nd"(port));

    kmutex_unlock(&global_portio_table_lock);
    return 0;
}

int kportio_outb(portio_handle_t handle, uint32_t offset, uint8_t value)
{
    uint16_t port;

    kmutex_lock(&global_portio_table_lock);
    if (kportio_resolve_port_locked(handle, offset, sizeof(value), &port) != 0) {
        kmutex_unlock(&global_portio_table_lock);
        return -1;
    }

    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));

    kmutex_unlock(&global_portio_table_lock);
    return 0;
}

int kportio_outw(portio_handle_t handle, uint32_t offset, uint16_t value)
{
    uint16_t port;

    kmutex_lock(&global_portio_table_lock);
    if (kportio_resolve_port_locked(handle, offset, sizeof(value), &port) != 0) {
        kmutex_unlock(&global_portio_table_lock);
        return -1;
    }

    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));

    kmutex_unlock(&global_portio_table_lock);
    return 0;
}

int kportio_outl(portio_handle_t handle, uint32_t offset, uint32_t value)
{
    uint16_t port;

    kmutex_lock(&global_portio_table_lock);
    if (kportio_resolve_port_locked(handle, offset, sizeof(value), &port) != 0) {
        kmutex_unlock(&global_portio_table_lock);
        return -1;
    }

    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));

    kmutex_unlock(&global_portio_table_lock);
    return 0;
}
