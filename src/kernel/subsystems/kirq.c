#include <stdint.h>
#include <string.h>

#include <stdbool.h>

#include <sharkix/kernel/memory.h>
#include <sharkix/kernel/subsystems/kirq.h>
#include <sharkix/kernel/subsystems/portio.h>
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

static void kirq_free_notify_bindings(irq_notify_binding_t *bindings) {
    while (bindings) {
        irq_notify_binding_t *next = bindings->next;

        knotify_release(bindings->notify);
        kfree(bindings);
        bindings = next;
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

static int kirq_find_by_hwirq(irq_handle_t* out, uint32_t hwirq) {
	irq_t *irq;
	irq_t *tmp;

	if (!out || hwirq >= IRQ_COUNT)
	    return -1;

	HASH_ITER(hh, global_irq_table, irq, tmp) {
	    if (irq->hwirq == hwirq) {
	        *out = irq->handle;
	        return 0;
	    }
	}

	return -1;
}

int kirq_create(irq_handle_t *out, uint32_t hwirq) {
    kirq_flags_t flags;

    if (!out || hwirq >= IRQ_COUNT)
         return -1;

    kmutex_lock(&global_irq_table_lock);

    // if there's already another irq_t, just return the same handle
    if (kirq_find_by_hwirq(out, hwirq) == 0) {
	    kmutex_unlock(&global_irq_table_lock);
	    return 0;
    }
    
    irq_t *irq;
    int result;

    irq = kmalloc(sizeof(*irq));
    if (!irq) {
        kmutex_unlock(&global_irq_table_lock);
	return -1; // TODO: we should probably return a memory allocation error or consider a kpanic or a generic OOM or something here
    }

    memset(irq, 0, sizeof(*irq));

    irq->hwirq = hwirq;
    ksem_init(&irq->sem, 0);

    result = kirq_insert_locked(irq);

    if(result != 0) {
       kfree(irq);
       kmutex_unlock(&global_irq_table_lock);
       return -1;
    }

    flags = kspin_lock_irqsave(&hwirq_table_lock);

    kirq_attach_hwirq_locked(irq);

    kspin_unlock_irqrestore(&hwirq_table_lock, flags);

    *out = irq->handle;
    kmutex_unlock(&global_irq_table_lock);
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
    // NOTE TO FUTURE GWEN, FUTURE OTHER DEVS (HUMAN OR AI):
    // don't change this to be more than the "mostly NOP" it currently is without asking
    // that would be a huge redesign, don't do it!

    // this function is mostly a NOP,  but should still do some basic checks for correctness
    if (handle == IRQ_INVALID_HANDLE)
        return -1;

    irq_t *irq;
    irq_notify_binding_t *free_bindings = NULL;
    kirq_flags_t flags;

    kmutex_lock(&global_irq_table_lock);
    irq = kirq_find_locked(handle);
    if (!irq) {
        kmutex_unlock(&global_irq_table_lock);
        return -1;
    }

    flags = kspin_lock_irqsave(&hwirq_table_lock);
    free_bindings = irq->notify_bindings;
    irq->notify_bindings = NULL;
    kspin_unlock_irqrestore(&hwirq_table_lock, flags);
    kmutex_unlock(&global_irq_table_lock);

    kirq_free_notify_bindings(free_bindings);

    // we don't actually destroy IRQs, because that's nonsense
    // but we should still do the above checks
    // at some point we might "detatch" pending waiters or something here
    // but for now, we just return 0
    return 0;
}

void kirq_handle(uint64_t hwirq) {
    irq_t *irq;
    irq_notify_binding_t *binding;
    kirq_flags_t flags;

    if (hwirq >= IRQ_COUNT)
        return;

    flags = kspin_lock_irqsave(&hwirq_table_lock);

    irq = hwirq_table[hwirq];
    while (irq) {
        ksem_post(&irq->sem);

        for (binding = irq->notify_bindings;
             binding;
             binding = binding->next) {
            knotify_signal_ref(binding->notify, binding->bits);
        }

        irq = irq->irq_next;
    }

    kspin_unlock_irqrestore(&hwirq_table_lock, flags);
}

int kirq_bind_notify(irq_handle_t irq_handle,
                     notify_handle_t notify_handle,
                     uint64_t bits) {
    irq_notify_binding_t *binding;
    irq_notify_binding_t *new_binding;
    notify_t *notify;
    irq_t *irq;
    kirq_flags_t flags;

    if (irq_handle == IRQ_INVALID_HANDLE ||
        notify_handle == NOTIFY_INVALID_HANDLE || bits == 0)
        return -1;

    notify = knotify_acquire(notify_handle);
    if (!notify)
        return -1;

    new_binding = kmalloc(sizeof(*new_binding));
    if (!new_binding) {
        knotify_release(notify);
        return -1;
    }

    new_binding->notify = notify;
    new_binding->bits = bits;
    new_binding->next = NULL;

    kmutex_lock(&global_irq_table_lock);
    irq = kirq_find_locked(irq_handle);
    if (!irq) {
        kmutex_unlock(&global_irq_table_lock);
        kfree(new_binding);
        knotify_release(notify);
        return -1;
    }

    flags = kspin_lock_irqsave(&hwirq_table_lock);
    for (binding = irq->notify_bindings; binding; binding = binding->next) {
        if (binding->notify == notify) {
            binding->bits = bits;
            kspin_unlock_irqrestore(&hwirq_table_lock, flags);
            kmutex_unlock(&global_irq_table_lock);
            kfree(new_binding);
            knotify_release(notify);
            return 0;
        }
    }

    new_binding->next = irq->notify_bindings;
    irq->notify_bindings = new_binding;
    kspin_unlock_irqrestore(&hwirq_table_lock, flags);
    kmutex_unlock(&global_irq_table_lock);

    return 0;
}

int kirq_unbind_notify(irq_handle_t irq_handle,
                       notify_handle_t notify_handle) {
    irq_notify_binding_t **current;
    irq_notify_binding_t *binding;
    irq_notify_binding_t *free_binding = NULL;
    irq_t *irq;
    kirq_flags_t flags;

    if (irq_handle == IRQ_INVALID_HANDLE ||
        notify_handle == NOTIFY_INVALID_HANDLE)
        return -1;

    kmutex_lock(&global_irq_table_lock);
    irq = kirq_find_locked(irq_handle);
    if (!irq) {
        kmutex_unlock(&global_irq_table_lock);
        return -1;
    }

    flags = kspin_lock_irqsave(&hwirq_table_lock);
    current = &irq->notify_bindings;
    while (*current && (*current)->notify->handle != notify_handle)
        current = &(*current)->next;

    binding = *current;
    if (!binding) {
        kspin_unlock_irqrestore(&hwirq_table_lock, flags);
        kmutex_unlock(&global_irq_table_lock);
        return -1;
    }

    *current = binding->next;
    binding->next = NULL;
    free_binding = binding;
    kspin_unlock_irqrestore(&hwirq_table_lock, flags);
    kmutex_unlock(&global_irq_table_lock);

    if (free_binding)
        kirq_free_notify_bindings(free_binding);
    return 0;
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
