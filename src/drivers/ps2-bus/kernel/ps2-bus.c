#include <stddef.h>
#include <stdint.h>

#include <sharkix/kernel/subsystems/kcaps.h>
#include "console.h"
#include <sharkix/kernel/subsystems/kipc.h>
#include "memory.h"
#include "program.h"
#include "thread.h"
#include <sharkix/kernel/ipc_registry.h>
#include <sharkix/kernel/subsystems/kirq.h>
#include <sharkix/kernel/subsystems/knotify.h>
#include <sharkix/kernel/subsystems/portio.h>

enum {
    PS2BUS_BOOTSTRAP_CAPS = 10,
    PS2BUS_STACK_WORDS = PS2BUS_BOOTSTRAP_CAPS + 1,
    PS2BUS_STACK_BYTES = PS2BUS_STACK_WORDS * sizeof(uint64_t),
    PS2BUS_IRQ1 = 1,
    PS2BUS_IRQ12 = 12,
    PS2BUS_DATA_PORT = 0x60,
    PS2BUS_COMMAND_PORT = 0x64,
    PS2BUS_IRQ1_BIT = 0,
    PS2BUS_IRQ12_BIT = 1,
    PS2BUS_PORT1_TX_BIT = 2,
    PS2BUS_PORT2_TX_BIT = 3
};

#define PS2BUS_READY_BIT (UINT64_C(1) << 63)

extern const uint8_t ps2_bus_image_start[];
extern const uint8_t ps2_bus_image_end[];

static bool ps2bus_is_ready;

static irq_handle_t ps2bus_irq1      = IRQ_INVALID_HANDLE;
static cap_handle_t ps2bus_irq1_cap  = CAP_INVALID_HANDLE;

static irq_handle_t ps2bus_irq12     = IRQ_INVALID_HANDLE;
static cap_handle_t ps2bus_irq12_cap = CAP_INVALID_HANDLE;

static portio_handle_t ps2bus_data_port  = PORTIO_INVALID_HANDLE;
static cap_handle_t ps2bus_data_port_cap = CAP_INVALID_HANDLE;

static portio_handle_t ps2bus_command_port  = PORTIO_INVALID_HANDLE;
static cap_handle_t ps2bus_command_port_cap = CAP_INVALID_HANDLE;

// the controller has only two ports
static ipc_handle_t ps2bus_port1_tx_ep  = IPC_INVALID_HANDLE;
static cap_handle_t ps2bus_port1_tx_cap = CAP_INVALID_HANDLE;
static ipc_handle_t ps2bus_port1_rx_ep  = IPC_INVALID_HANDLE;
static cap_handle_t ps2bus_port1_rx_cap = CAP_INVALID_HANDLE;

static ipc_handle_t ps2bus_port2_tx_ep  = IPC_INVALID_HANDLE;
static cap_handle_t ps2bus_port2_tx_cap = CAP_INVALID_HANDLE;
static ipc_handle_t ps2bus_port2_rx_ep  = IPC_INVALID_HANDLE;
static cap_handle_t ps2bus_port2_rx_cap = CAP_INVALID_HANDLE;

static notify_handle_t ps2bus_ready     = NOTIFY_INVALID_HANDLE;
static cap_handle_t    ps2bus_ready_cap = CAP_INVALID_HANDLE;
static notify_handle_t ps2bus_events     = NOTIFY_INVALID_HANDLE;
static cap_handle_t    ps2bus_events_cap = CAP_INVALID_HANDLE;

static int ps2bus_create_user_task(address_space_t **out_as,
                                   thread_t **out_thread,
                                   uint64_t **out_bootstrap)
{
    address_space_t *address_space;
    uintptr_t stack_top;
    uint64_t physical;
    thread_create_params_t params;
    const program_image_t image = {
        .data = ps2_bus_image_start,
        .size = (size_t)(ps2_bus_image_end - ps2_bus_image_start)
    };

    address_space = address_space_create(0);
    if (!address_space ||
        program_map_flat_image(address_space, &image,
                               PROGRAM_DEFAULT_LOAD_ADDRESS) != 0 ||
        program_map_user_stack(address_space, PROGRAM_DEFAULT_STACK_BASE,
                               PAGE_SIZE, &stack_top) != 0) {
        return -1;
    }

    physical = address_space_translate(address_space,
                                       stack_top - PS2BUS_STACK_BYTES);
    if (physical == UINT64_MAX) {
        return -1;
    }

    params = (thread_create_params_t) {
        .entry_rip = PROGRAM_DEFAULT_LOAD_ADDRESS,
        .initial_stack_pointer = stack_top - PS2BUS_STACK_BYTES,
        .name = "ps2bus", .priority = THREAD_PRIORITY_NORMAL
    };
    *out_thread = thread_create(address_space, THREAD_PRIVILEGE_USER, &params);
    if (!*out_thread) {
        return -1;
    }

    *out_as = address_space;
    *out_bootstrap = (uint64_t *)phys_to_virt(physical);
    return 0;
}

void ps2bus_init(void)
{
    address_space_t *user_as = NULL;
    thread_t *user_thread = NULL;
    uint64_t *bootstrap = NULL;

    if (kirq_create(&ps2bus_irq1, PS2BUS_IRQ1) != 0 ||
        kcap_create(ps2bus_irq1, CAP_TYPE_IRQ,
                    CAP_RIGHT_IRQ_WAIT | CAP_RIGHT_IRQ_ACK | CAP_RIGHT_GETNAME,
                    &ps2bus_irq1_cap) != 0 ||
        kirq_create(&ps2bus_irq12, PS2BUS_IRQ12) != 0 ||
        kcap_create(ps2bus_irq12, CAP_TYPE_IRQ,
                    CAP_RIGHT_IRQ_WAIT | CAP_RIGHT_IRQ_ACK | CAP_RIGHT_GETNAME,
                    &ps2bus_irq12_cap) != 0 ||
        kportio_create(&ps2bus_data_port, PS2BUS_DATA_PORT, 1) != 0 ||
        kcap_create(ps2bus_data_port, CAP_TYPE_PORTIO,
                    CAP_RIGHT_PORTIO_READ | CAP_RIGHT_PORTIO_WRITE |
                    CAP_RIGHT_GETNAME,
                    &ps2bus_data_port_cap) != 0 ||
        kportio_create(&ps2bus_command_port, PS2BUS_COMMAND_PORT, 1) != 0 ||
        kcap_create(ps2bus_command_port, CAP_TYPE_PORTIO,
                    CAP_RIGHT_PORTIO_READ | CAP_RIGHT_PORTIO_WRITE |
                    CAP_RIGHT_GETNAME,
                    &ps2bus_command_port_cap) != 0 ||
        kipc_create(&ps2bus_port1_tx_ep) != IPC_OK ||
        kcap_create(ps2bus_port1_tx_ep, CAP_TYPE_IPC,
                    CAP_RIGHT_IPC_RECV | CAP_RIGHT_GETNAME,
                    &ps2bus_port1_tx_cap) != 0 ||
        kipc_create(&ps2bus_port1_rx_ep) != IPC_OK ||
        kcap_create(ps2bus_port1_rx_ep, CAP_TYPE_IPC,
                    CAP_RIGHT_IPC_SEND | CAP_RIGHT_GETNAME,
                    &ps2bus_port1_rx_cap) != 0 ||
        kipc_create(&ps2bus_port2_tx_ep) != IPC_OK ||
        kcap_create(ps2bus_port2_tx_ep, CAP_TYPE_IPC,
                    CAP_RIGHT_IPC_RECV | CAP_RIGHT_GETNAME,
                    &ps2bus_port2_tx_cap) != 0 ||
        kipc_create(&ps2bus_port2_rx_ep) != IPC_OK ||
        kcap_create(ps2bus_port2_rx_ep, CAP_TYPE_IPC,
                    CAP_RIGHT_IPC_SEND | CAP_RIGHT_GETNAME,
                    &ps2bus_port2_rx_cap) != 0 ||
        knotify_create(&ps2bus_ready) != 0 ||
        kcap_create(ps2bus_ready, CAP_TYPE_NOTIFY,
                    CAP_RIGHT_NOTIFY_SIGNAL | CAP_RIGHT_GETNAME,
                    &ps2bus_ready_cap) != 0 ||
        knotify_create(&ps2bus_events) != 0 ||
        kcap_create(ps2bus_events, CAP_TYPE_NOTIFY,
                    CAP_RIGHT_NOTIFY_WAIT | CAP_RIGHT_NOTIFY_ACK |
                    CAP_RIGHT_GETNAME,
                    &ps2bus_events_cap) != 0 ||
        kirq_bind_notify(ps2bus_irq1, ps2bus_events,
                         UINT64_C(1) << PS2BUS_IRQ1_BIT) != 0 ||
        kirq_bind_notify(ps2bus_irq12, ps2bus_events,
                         UINT64_C(1) << PS2BUS_IRQ12_BIT) != 0 ||
        kipc_bind_notify(ps2bus_port1_tx_ep, ps2bus_events,
                        UINT64_C(1) << PS2BUS_PORT1_TX_BIT) != IPC_OK ||
        kipc_bind_notify(ps2bus_port2_tx_ep, ps2bus_events,
                        UINT64_C(1) << PS2BUS_PORT2_TX_BIT) != IPC_OK) {
        console_write("ps2bus capability setup failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    if (ps2bus_create_user_task(&user_as, &user_thread, &bootstrap) != 0 ||
        kcap_set_name(ps2bus_irq1_cap, "ps2bus.irq.1",
                      sizeof("ps2bus.irq.1") - 1) != 0 ||
        kcap_set_name(ps2bus_data_port_cap, "ps2bus.pio.data",
                      sizeof("ps2bus.pio.data") - 1) != 0 ||
        kcap_set_name(ps2bus_command_port_cap, "ps2bus.pio.cmd",
                      sizeof("ps2bus.pio.cmd") - 1) != 0 ||
        kcap_set_name(ps2bus_irq12_cap, "ps2bus.irq.12",
                      sizeof("ps2bus.irq.12") - 1) != 0 ||
        kcap_set_name(ps2bus_port1_tx_cap, "ps2.port1.tx",
                      sizeof("ps2.port1.tx") - 1) != 0 ||
        kcap_set_name(ps2bus_port1_rx_cap, "ps2.port1.rx",
                      sizeof("ps2.port1.rx") - 1) != 0 ||
        kcap_set_name(ps2bus_port2_tx_cap, "ps2.port2.tx",
                      sizeof("ps2.port2.tx") - 1) != 0 ||
        kcap_set_name(ps2bus_port2_rx_cap, "ps2.port2.rx",
                      sizeof("ps2.port2.rx") - 1) != 0 ||
        kcap_set_name(ps2bus_ready_cap, "ps2bus.notify",
                      sizeof("ps2bus.notify") - 1) != 0 ||
        kcap_set_name(ps2bus_events_cap, "ps2bus.events",
                      sizeof("ps2bus.events") - 1) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_irq1_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_irq12_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_data_port_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_command_port_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_port1_tx_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_port1_rx_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_port2_tx_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_port2_rx_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_ready_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_events_cap) != 0 ||
        kipc_registry_register("ps2.port1.tx", ps2bus_port1_tx_ep) != 0 ||
        kipc_registry_register("ps2.port1.rx", ps2bus_port1_rx_ep) != 0 ||
        kipc_registry_register("ps2.port2.tx", ps2bus_port2_tx_ep) != 0 ||
        kipc_registry_register("ps2.port2.rx", ps2bus_port2_rx_ep) != 0 ||
        kipc_registry_register("ps2.port1", ps2bus_port1_rx_ep) != 0) {
        console_write("ps2bus task setup failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    bootstrap[0] = PS2BUS_BOOTSTRAP_CAPS;
    bootstrap[1] = ps2bus_irq1_cap;
    bootstrap[2] = ps2bus_irq12_cap;
    bootstrap[3] = ps2bus_data_port_cap;
    bootstrap[4] = ps2bus_command_port_cap;
    bootstrap[5] = ps2bus_port1_tx_cap;
    bootstrap[6] = ps2bus_port1_rx_cap;
    bootstrap[7] = ps2bus_port2_tx_cap;
    bootstrap[8] = ps2bus_port2_rx_cap;
    bootstrap[9] = ps2bus_ready_cap;
    bootstrap[10] = ps2bus_events_cap;

    if (thread_start(user_thread) != 0) {
        console_write("ps2bus thread startup failed\n");
    }
}

bool ps2bus_isready(void)
{
    uint64_t pending = 0;

    if (!ps2bus_is_ready && ps2bus_ready != NOTIFY_INVALID_HANDLE &&
        knotify_poll(ps2bus_ready, &pending) == 0 &&
        (pending & PS2BUS_READY_BIT) != 0) {
        ps2bus_is_ready = true;
    }
    return ps2bus_is_ready;
}
