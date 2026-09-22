#include <stdint.h>
#include <string.h>

#include <stdbool.h>

#include <sharkix/kernel/memory.h>
#include <sharkix/kernel/irq.h>
#include <sharkix/kernel/portio.h>
#include <sharkix/kernel/console.h>
#include <sharkix/kernel/sync.h>

#include <sharkix/kernel/uthash.h>

#define MASTER_PIC_PORT ((uint16_t)0x20)
#define SLAVE_PIC_PORT  ((uint16_t)0xA0)
#define PIC_PORT_LENGTH ((uint32_t)2)

static portio_handle_t pio_master_pic = PORTIO_INVALID_HANDLE;
static portio_handle_t pio_slave_pic  = PORTIO_INVALID_HANDLE;

static irq_t *global_irq_table;
static irq_handle_t next_irq_handle;
static kmutex_t global_irq_table_lock;

static irq_t *hwirq_table[IRQ_COUNT];
static kspinlock_t hwirq_table_lock;

static bool irq_subsys_ready = false;

static irq_t *kirq_find_locked(irq_handle_t handle) {
    irq_t *irq = NULL;
    uint32_t hashv = (uint32_t)handle;

    HASH_FIND_BYHASHVALUE(
        hh,
        global_irq_table,
        &handle,
        sizeof(handle),
        hashv,
        irq
    );

    return irq;
}

static int kirq_insert_locked(irq_t *irq) {
    uint32_t hashv;

    if (next_irq_handle == IRQ_INVALID_HANDLE)
        return -1;

    irq->handle = next_irq_handle++;
    hashv = (uint32_t)irq->handle;

    HASH_ADD_BYHASHVALUE(
        hh,
        global_irq_table,
        handle,
        sizeof(irq->handle),
        hashv,
        irq
    );

    return 0;
}

static void kirq_attach_hwirq_locked(irq_t *irq) {
    irq->irq_next = hwirq_table[irq->hwirq];
    hwirq_table[irq->hwirq] = irq;
}

static void kirq_detach_hwirq_locked(irq_t *irq) {
    irq_t **current = &hwirq_table[irq->hwirq];

    while (*current) {
        if (*current == irq) {
            *current = irq->irq_next;
            irq->irq_next = NULL;
            return;
        }

        current = &(*current)->irq_next;
    }
}

void kirq_init(void) {
    global_irq_table = NULL;
    next_irq_handle = 1;

    memset(hwirq_table, 0, sizeof(hwirq_table));

    kmutex_init(&global_irq_table_lock);
    kspin_init(&hwirq_table_lock);

    if (kportio_create(&pio_master_pic,
                       MASTER_PIC_PORT,
                       PIC_PORT_LENGTH) != 0) {
        console_write("kirq_init() - failed to obtain PortIO for master PIC!\n");
        for (;;);
    }

    if (kportio_create(&pio_slave_pic,
                       SLAVE_PIC_PORT,
                       PIC_PORT_LENGTH) != 0) {
        console_write("kirq_init() - failed to obtain PortIO for slave PIC!\n");
        for (;;);
    }
    irq_subsys_ready = true;
}

int kirq_create(irq_handle_t *out, uint32_t hwirq) {
    irq_t *irq;
    int result;

    if (!out || hwirq >= IRQ_COUNT)
        return -1;

    irq = kmalloc(sizeof(*irq));
    if (!irq)
        return -1;

    memset(irq, 0, sizeof(*irq));

    irq->hwirq = hwirq;
    ksem_init(&irq->sem, 0);

    kmutex_lock(&global_irq_table_lock);

    result = kirq_insert_locked(irq);

    kmutex_unlock(&global_irq_table_lock);

    if (result != 0) {
        kfree(irq);
        return -1;
    }

    kspin_lock(&hwirq_table_lock);

    kirq_attach_hwirq_locked(irq);

    kspin_unlock(&hwirq_table_lock);

    *out = irq->handle;
    return 0;
}

int kirq_get(irq_handle_t handle, irq_t *out) {
    irq_t *irq;

    if (!out || handle == IRQ_INVALID_HANDLE)
        return -1;

    kmutex_lock(&global_irq_table_lock);

    irq = kirq_find_locked(handle);
    if (!irq) {
        kmutex_unlock(&global_irq_table_lock);
        return -1;
    }

    out->handle = irq->handle;
    out->hwirq = irq->hwirq;

    kmutex_unlock(&global_irq_table_lock);

    return 0;
}

int kirq_wait(irq_handle_t handle) {
    irq_t *irq;

    if (handle == IRQ_INVALID_HANDLE)
        return -1;

    kmutex_lock(&global_irq_table_lock);

    irq = kirq_find_locked(handle);
    if (!irq) {
        kmutex_unlock(&global_irq_table_lock);
        return -1;
    }

    kmutex_unlock(&global_irq_table_lock);

    ksem_wait(&irq->sem);

    return 0;
}

int kirq_destroy(irq_handle_t handle) {
    irq_t *irq;

    if (handle == IRQ_INVALID_HANDLE)
        return -1;

    kmutex_lock(&global_irq_table_lock);

    irq = kirq_find_locked(handle);
    if (!irq) {
        kmutex_unlock(&global_irq_table_lock);
        return -1;
    }

    /*
     * Remove it from hardware dispatch first. Once this lock is released,
     * kirq_handle() can no longer discover this object.
     */
    kspin_lock(&hwirq_table_lock);
    kirq_detach_hwirq_locked(irq);
    kspin_unlock(&hwirq_table_lock);

    /*
     * Remove the handle, making the object inaccessible through the normal
     * IRQ subsystem API.
     */
    HASH_DEL(global_irq_table, irq);

    kmutex_unlock(&global_irq_table_lock);

    /*
     * Do not free yet. A thread may already have resolved this object and
     * be sleeping in ksem_wait(). Proper reclamation needs lifetime
     * management/refcounting.
     */
    return 0;
}

void kirq_handle(uint64_t hwirq) {
    irq_t *irq;

    if (hwirq >= IRQ_COUNT)
        return;

    kspin_lock(&hwirq_table_lock);

    irq = hwirq_table[hwirq];

    while (irq) {
        ksem_post(&irq->sem);
        irq = irq->irq_next;
    }

    kspin_unlock(&hwirq_table_lock);
}

static int pic_eoi(uint32_t hwirq) {
    /*
     * IRQs 8-15 arrive through the slave PIC, which is cascaded
     * through IRQ2 on the master. Acknowledge slave first.
     */
    if (hwirq >= 8) {
        if (kportio_outb(pio_slave_pic, 0, 0x20) != 0)
            return -1;
    }

    if (kportio_outb(pio_master_pic, 0, 0x20) != 0)
        return -1;

    return 0;
}

int kirq_ack(irq_handle_t handle) {
    irq_t *irq;
    uint32_t hwirq;

    if (handle == IRQ_INVALID_HANDLE)
        return -1;

    kmutex_lock(&global_irq_table_lock);

    irq = kirq_find_locked(handle);
    if (!irq) {
        kmutex_unlock(&global_irq_table_lock);
        return -1;
    }

    hwirq = irq->hwirq;

    kmutex_unlock(&global_irq_table_lock);

    return pic_eoi(hwirq);
}


void kirq_handler(uint64_t irq) {
     if(irq_subsys_ready) kirq_handle(irq);
}
