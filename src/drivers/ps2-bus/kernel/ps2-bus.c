#include <stddef.h>
#include <stdint.h>

#include "caps.h"
#include "console.h"
#include "ipc.h"
#include "memory.h"
#include "program.h"
#include "thread.h"
#include <sharkix/kernel/irq.h>
#include <sharkix/kernel/portio.h>

enum {
    PS2BUS_STACK_WORDS = 6,
    PS2BUS_STACK_BYTES = PS2BUS_STACK_WORDS * sizeof(uint64_t),
    PS2BUS_IRQ1 = 1,
    PS2BUS_DATA_PORT = 0x60,
    PS2BUS_COMMAND_PORT = 0x64
};

extern const uint8_t ps2_bus_image_start[];
extern const uint8_t ps2_bus_image_end[];

static bool ps2bus_ready;

static irq_handle_t ps2bus_irq1 = IRQ_INVALID_HANDLE;
static cap_handle_t ps2bus_irq1_cap = CAP_INVALID_HANDLE;

static portio_handle_t ps2bus_data_port = PORTIO_INVALID_HANDLE;
static cap_handle_t ps2bus_data_port_cap = CAP_INVALID_HANDLE;

static portio_handle_t ps2bus_command_port = PORTIO_INVALID_HANDLE;
static cap_handle_t ps2bus_command_port_cap = CAP_INVALID_HANDLE;

/* The user task sends keyboard scancodes to this endpoint. */
static ipc_handle_t ps2bus_port1_endpoint = IPC_INVALID_HANDLE;
static cap_handle_t ps2bus_port1_cap = CAP_INVALID_HANDLE;

/* The user task signals this endpoint after it has configured the controller. */
static ipc_handle_t ps2bus_ready_endpoint = IPC_INVALID_HANDLE;
static cap_handle_t ps2bus_ready_cap = CAP_INVALID_HANDLE;

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
        ipc_create(&ps2bus_port1_endpoint) != IPC_OK ||
        kcap_create(ps2bus_port1_endpoint, CAP_TYPE_IPC_ENDPOINT,
                    CAP_RIGHT_IPC_SEND | CAP_RIGHT_GETNAME,
                    &ps2bus_port1_cap) != 0 ||
        ipc_create(&ps2bus_ready_endpoint) != IPC_OK ||
        kcap_create(ps2bus_ready_endpoint, CAP_TYPE_IPC_ENDPOINT,
                    CAP_RIGHT_IPC_SEND | CAP_RIGHT_GETNAME,
                    &ps2bus_ready_cap) != 0) {
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
        kcap_set_name(ps2bus_port1_cap, "ps2bus.port1",
                      sizeof("ps2bus.port1") - 1) != 0 ||
        kcap_set_name(ps2bus_ready_cap, "ps2bus.ready",
                      sizeof("ps2bus.ready") - 1) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_irq1_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_data_port_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_command_port_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_port1_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2bus_ready_cap) != 0) {
        console_write("ps2bus task setup failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    bootstrap[0] = 5;
    bootstrap[1] = ps2bus_irq1_cap;
    bootstrap[2] = ps2bus_data_port_cap;
    bootstrap[3] = ps2bus_command_port_cap;
    bootstrap[4] = ps2bus_port1_cap;
    bootstrap[5] = ps2bus_ready_cap;

    if (thread_start(user_thread) != 0) {
        console_write("ps2bus thread startup failed\n");
    }
}

int ps2bus_getc(void)
{
    ipc_message_t message = { 0 };

    if (ipc_recv(ps2bus_port1_endpoint, &message) != IPC_OK) {
        return -1;
    }
    return (int)(uint8_t)message.words[0];
}

bool ps2bus_isready(void)
{
    ipc_message_t message = { 0 };

    if (!ps2bus_ready && ipc_recv_nb(ps2bus_ready_endpoint, &message) == IPC_OK) {
        ps2bus_ready = true;
    }
    return ps2bus_ready;
}
