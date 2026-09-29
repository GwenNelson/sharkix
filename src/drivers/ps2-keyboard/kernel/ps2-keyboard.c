#include <stddef.h>
#include <stdint.h>

#include "caps.h"
#include "console.h"
#include "ipc.h"
#include "ipc_registry.h"
#include "memory.h"
#include "program.h"
#include "thread.h"
#include <sharkix/kernel/notification.h>

enum {
    PS2_KEYBOARD_BOOTSTRAP_CAPS = 6,
    PS2_KEYBOARD_STACK_WORDS = PS2_KEYBOARD_BOOTSTRAP_CAPS + 1,
    PS2_KEYBOARD_STACK_BYTES = PS2_KEYBOARD_STACK_WORDS * sizeof(uint64_t),
    PS2_KEYBOARD_READY_BIT = 63
};

extern const uint8_t ps2_keyboard_image_start[];
extern const uint8_t ps2_keyboard_image_end[];

static ipc_handle_t ps2_keyboard_port1_tx = IPC_INVALID_HANDLE;
static cap_handle_t ps2_keyboard_port1_tx_cap = CAP_INVALID_HANDLE;
static ipc_handle_t ps2_keyboard_port1_rx = IPC_INVALID_HANDLE;
static cap_handle_t ps2_keyboard_port1_rx_cap = CAP_INVALID_HANDLE;
static ipc_handle_t ps2_keyboard_port2_tx = IPC_INVALID_HANDLE;
static cap_handle_t ps2_keyboard_port2_tx_cap = CAP_INVALID_HANDLE;
static ipc_handle_t ps2_keyboard_port2_rx = IPC_INVALID_HANDLE;
static cap_handle_t ps2_keyboard_port2_rx_cap = CAP_INVALID_HANDLE;
static ipc_handle_t ps2_keyboard_output = IPC_INVALID_HANDLE;
static cap_handle_t ps2_keyboard_output_cap = CAP_INVALID_HANDLE;
static notify_handle_t ps2_keyboard_ready = NOTIFY_INVALID_HANDLE;
static cap_handle_t ps2_keyboard_ready_cap = CAP_INVALID_HANDLE;
static bool ps2_keyboard_ready_seen;

static int ps2_keyboard_create_user_task(address_space_t **out_as,
                                         thread_t **out_thread,
                                         uint64_t **out_bootstrap)
{
    address_space_t *address_space;
    uintptr_t stack_top;
    uint64_t physical;
    thread_create_params_t params;
    const program_image_t image = {
        .data = ps2_keyboard_image_start,
        .size = (size_t)(ps2_keyboard_image_end -
                         ps2_keyboard_image_start)
    };

    address_space = address_space_create(0);
    if (!address_space ||
        program_map_flat_image(address_space, &image,
                               PROGRAM_DEFAULT_LOAD_ADDRESS) != 0 ||
        program_map_user_stack(address_space, PROGRAM_DEFAULT_STACK_BASE,
                               PAGE_SIZE, &stack_top) != 0)
        return -1;

    physical = address_space_translate(address_space,
                                       stack_top - PS2_KEYBOARD_STACK_BYTES);
    if (physical == UINT64_MAX)
        return -1;

    params = (thread_create_params_t) {
        .entry_rip = PROGRAM_DEFAULT_LOAD_ADDRESS,
        .initial_stack_pointer = stack_top - PS2_KEYBOARD_STACK_BYTES,
        .name = "ps2-keyboardd", .priority = THREAD_PRIORITY_NORMAL
    };
    *out_thread = thread_create(address_space, THREAD_PRIVILEGE_USER, &params);
    if (!*out_thread)
        return -1;

    *out_as = address_space;
    *out_bootstrap = (uint64_t *)phys_to_virt(physical);
    return 0;
}

void ps2_keyboard_init(void)
{
    address_space_t *user_as = NULL;
    thread_t *user_thread = NULL;
    uint64_t *bootstrap = NULL;

    if (kipc_registry_lookup("ps2.port1.tx", &ps2_keyboard_port1_tx) != 0 ||
        kipc_registry_lookup("ps2.port1.rx", &ps2_keyboard_port1_rx) != 0 ||
        kipc_registry_lookup("ps2.port2.tx", &ps2_keyboard_port2_tx) != 0 ||
        kipc_registry_lookup("ps2.port2.rx", &ps2_keyboard_port2_rx) != 0 ||
        kcap_create(ps2_keyboard_port1_tx, CAP_TYPE_IPC_ENDPOINT,
                    CAP_RIGHT_IPC_SEND | CAP_RIGHT_GETNAME,
                    &ps2_keyboard_port1_tx_cap) != 0 ||
        kcap_create(ps2_keyboard_port1_rx, CAP_TYPE_IPC_ENDPOINT,
                    CAP_RIGHT_IPC_RECV | CAP_RIGHT_GETNAME,
                    &ps2_keyboard_port1_rx_cap) != 0 ||
        kcap_create(ps2_keyboard_port2_tx, CAP_TYPE_IPC_ENDPOINT,
                    CAP_RIGHT_IPC_SEND | CAP_RIGHT_GETNAME,
                    &ps2_keyboard_port2_tx_cap) != 0 ||
        kcap_create(ps2_keyboard_port2_rx, CAP_TYPE_IPC_ENDPOINT,
                    CAP_RIGHT_IPC_RECV | CAP_RIGHT_GETNAME,
                    &ps2_keyboard_port2_rx_cap) != 0 ||
        ipc_create(&ps2_keyboard_output) != IPC_OK ||
        kcap_create(ps2_keyboard_output, CAP_TYPE_IPC_ENDPOINT,
                    CAP_RIGHT_IPC_SEND | CAP_RIGHT_GETNAME,
                    &ps2_keyboard_output_cap) != 0 ||
        knotify_create(&ps2_keyboard_ready) != 0 ||
        kcap_create(ps2_keyboard_ready, CAP_TYPE_NOTIFY,
                    CAP_RIGHT_NOTIFY_SIGNAL | CAP_RIGHT_GETNAME,
                    &ps2_keyboard_ready_cap) != 0) {
        console_write("ps2-keyboardd capability setup failed\n");
        return;
    }

    if (ps2_keyboard_create_user_task(&user_as, &user_thread, &bootstrap) != 0 ||
        kcap_set_name(ps2_keyboard_port1_tx_cap, "ps2.port1.tx",
                      sizeof("ps2.port1.tx") - 1) != 0 ||
        kcap_set_name(ps2_keyboard_port1_rx_cap, "ps2.port1.rx",
                      sizeof("ps2.port1.rx") - 1) != 0 ||
        kcap_set_name(ps2_keyboard_port2_tx_cap, "ps2.port2.tx",
                      sizeof("ps2.port2.tx") - 1) != 0 ||
        kcap_set_name(ps2_keyboard_port2_rx_cap, "ps2.port2.rx",
                      sizeof("ps2.port2.rx") - 1) != 0 ||
        kcap_set_name(ps2_keyboard_output_cap, "ps2.keyboard.output",
                      sizeof("ps2.keyboard.output") - 1) != 0 ||
        kcap_set_name(ps2_keyboard_ready_cap, "ps2-keyboard.notify",
                      sizeof("ps2-keyboard.notify") - 1) != 0 ||
        kcapset_addcap(user_as->capset, ps2_keyboard_port1_tx_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2_keyboard_port1_rx_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2_keyboard_port2_tx_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2_keyboard_port2_rx_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2_keyboard_output_cap) != 0 ||
        kcapset_addcap(user_as->capset, ps2_keyboard_ready_cap) != 0 ||
        kipc_registry_register("ps2.keyboard.output",
                               ps2_keyboard_output) != 0) {
        console_write("ps2-keyboardd task setup failed\n");
        return;
    }

    bootstrap[0] = PS2_KEYBOARD_BOOTSTRAP_CAPS;
    bootstrap[1] = ps2_keyboard_port1_tx_cap;
    bootstrap[2] = ps2_keyboard_port1_rx_cap;
    bootstrap[3] = ps2_keyboard_port2_tx_cap;
    bootstrap[4] = ps2_keyboard_port2_rx_cap;
    bootstrap[5] = ps2_keyboard_output_cap;
    bootstrap[6] = ps2_keyboard_ready_cap;

    if (thread_start(user_thread) != 0)
        console_write("ps2-keyboardd thread startup failed\n");
}

bool ps2_keyboard_isready(void)
{
    uint64_t pending = 0;

    if (!ps2_keyboard_ready_seen &&
        ps2_keyboard_ready != NOTIFY_INVALID_HANDLE &&
        knotify_poll(ps2_keyboard_ready, &pending) == 0 &&
        (pending & (UINT64_C(1) << PS2_KEYBOARD_READY_BIT)) != 0)
        ps2_keyboard_ready_seen = true;

    return ps2_keyboard_ready_seen;
}
