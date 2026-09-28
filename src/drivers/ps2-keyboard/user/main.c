#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

enum {
    PS2_KEYBOARD_BOOTSTRAP_CAPS = 6,
    PS2_KEYBOARD_READY_BIT = 63,
    PS2_COMMAND_IDENTIFY = 0xF2,
    PS2_COMMAND_DISABLE_SCANNING = 0xF5,
    PS2_COMMAND_SET_SCAN_CODE = 0xF0,
    PS2_COMMAND_ENABLE_SCANNING = 0xF4,
    PS2_RESPONSE_ACK = 0xFA,
    PS2_ID_PREFIX = 0xAB,
    PS2_ID_MF2_101 = 0x83,
    PS2_ID_MF2_101_ALT = 0x41,
    PS2_ID_MF2_101_ALT2 = 0xC1,
    PS2_SCAN_CODE_SET_2 = 0x02,
    PS2_PROBE_TIMEOUT_MS = 500,
    IPC_ERR_CANCELLED = -7
};

static uint64_t port1_tx_cap;
static uint64_t port1_rx_cap;
static uint64_t port2_tx_cap;
static uint64_t port2_rx_cap;
static uint64_t output_cap;
static uint64_t ready_cap;

static uint64_t ps2_keyboard_syscall(uint64_t number,
                                     uint64_t arg0,
                                     uint64_t arg1,
                                     uint64_t arg2)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = number;
    regs.rdi = arg0;
    regs.rsi = arg1;
    regs.rdx = arg2;
    (void)sharkix_syscall(&regs);
    return regs.rax;
}

static void ps2_keyboard_log(const char *message)
{
    sharkix_debug_puts("ps2-keyboardd: ");
    sharkix_debug_puts(message);
    sharkix_debug_puts("\n");
}

static void ps2_keyboard_stop(const char *message)
{
    ps2_keyboard_log(message);
    for (;;) {
        __asm__ volatile ("pause");
    }
}

static uint64_t ps2_keyboard_time_ms(void)
{
    return ps2_keyboard_syscall(SYSCALL_TIMER_MONOTONIC, 0, 0, 0);
}

static int ps2_keyboard_send_byte(uint64_t endpoint_cap, uint8_t byte)
{
    return (int64_t)ps2_keyboard_syscall(SYSCALL_IPC_SEND,
                                        endpoint_cap, byte, 0) == 0;
}

static int ps2_keyboard_try_receive_byte(uint64_t endpoint_cap,
                                         uint8_t *out_byte)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = SYSCALL_IPC_TRY_RECV;
    regs.rdi = endpoint_cap;
    (void)sharkix_syscall(&regs);

    if ((int64_t)regs.rax == IPC_ERR_CANCELLED)
        return 0;
    if ((int64_t)regs.rax != 0)
        return -1;

    *out_byte = (uint8_t)regs.rsi;
    return 1;
}

static int ps2_keyboard_receive_before(uint64_t endpoint_cap,
                                       uint64_t deadline,
                                       uint8_t *out_byte)
{
    for (;;) {
        int result = ps2_keyboard_try_receive_byte(endpoint_cap, out_byte);

        if (result != 0)
            return result;
        if (ps2_keyboard_time_ms() >= deadline)
            return 0;
        __asm__ volatile ("pause");
    }
}

static int ps2_keyboard_expect_ack(uint64_t rx_cap, uint64_t deadline)
{
    for (;;) {
        uint8_t response;
        int result = ps2_keyboard_receive_before(rx_cap, deadline, &response);

        if (result != 1)
            return 0;
        if (response == PS2_RESPONSE_ACK)
            return 1;
    }
}

static int ps2_keyboard_supported_id(uint8_t id)
{
    return id == PS2_ID_MF2_101 || id == PS2_ID_MF2_101_ALT ||
           id == PS2_ID_MF2_101_ALT2;
}

static int ps2_keyboard_probe(uint64_t tx_cap, uint64_t rx_cap)
{
    uint64_t deadline = ps2_keyboard_time_ms() + PS2_PROBE_TIMEOUT_MS;
    uint8_t id_prefix;
    uint8_t id;

    if (!ps2_keyboard_send_byte(tx_cap, PS2_COMMAND_IDENTIFY) ||
        !ps2_keyboard_expect_ack(rx_cap, deadline) ||
        ps2_keyboard_receive_before(rx_cap, deadline, &id_prefix) != 1 ||
        ps2_keyboard_receive_before(rx_cap, deadline, &id) != 1)
        return 0;

    return id_prefix == PS2_ID_PREFIX && ps2_keyboard_supported_id(id);
}

static int ps2_keyboard_initialize(uint64_t tx_cap, uint64_t rx_cap)
{
    uint64_t deadline;

    deadline = ps2_keyboard_time_ms() + PS2_PROBE_TIMEOUT_MS;
    if (!ps2_keyboard_send_byte(tx_cap, PS2_COMMAND_DISABLE_SCANNING) ||
        !ps2_keyboard_expect_ack(rx_cap, deadline))
        return 0;

    deadline = ps2_keyboard_time_ms() + PS2_PROBE_TIMEOUT_MS;
    if (!ps2_keyboard_send_byte(tx_cap, PS2_COMMAND_SET_SCAN_CODE) ||
        !ps2_keyboard_expect_ack(rx_cap, deadline))
        return 0;

    deadline = ps2_keyboard_time_ms() + PS2_PROBE_TIMEOUT_MS;
    if (!ps2_keyboard_send_byte(tx_cap, PS2_SCAN_CODE_SET_2) ||
        !ps2_keyboard_expect_ack(rx_cap, deadline))
        return 0;

    deadline = ps2_keyboard_time_ms() + PS2_PROBE_TIMEOUT_MS;
    if (!ps2_keyboard_send_byte(tx_cap, PS2_COMMAND_ENABLE_SCANNING) ||
        !ps2_keyboard_expect_ack(rx_cap, deadline))
        return 0;

    return 1;
}

static void ps2_keyboard_signal_ready(void)
{
    if ((int64_t)ps2_keyboard_syscall(SYSCALL_NOTIFY_SIGNAL,
                                      ready_cap,
                                      UINT64_C(1) << PS2_KEYBOARD_READY_BIT,
                                      0) != 0)
        ps2_keyboard_stop("failed signalling readiness");
}

static void ps2_keyboard_run(uint64_t tx_cap, uint64_t rx_cap)
{
    ps2_keyboard_log("using port 1");
    if (!ps2_keyboard_probe(tx_cap, rx_cap)) {
        ps2_keyboard_log("port 1 is not a supported keyboard; trying port 2");
        tx_cap = port2_tx_cap;
        rx_cap = port2_rx_cap;
        if (!ps2_keyboard_probe(tx_cap, rx_cap))
            ps2_keyboard_stop("no supported keyboard found on either port");
        ps2_keyboard_log("using port 2");
    }

    if (!ps2_keyboard_initialize(tx_cap, rx_cap))
        ps2_keyboard_stop("keyboard initialization failed");

    ps2_keyboard_signal_ready();
    ps2_keyboard_log("ready; forwarding scan-code bytes");

    for (;;) {
        sharkix_syscall_regs_t regs = { 0 };

        regs.rax = SYSCALL_IPC_RECV;
        regs.rdi = rx_cap;
        (void)sharkix_syscall(&regs);
        if ((int64_t)regs.rax != 0)
            ps2_keyboard_stop("keyboard receive failed");

        if (!ps2_keyboard_send_byte(output_cap, (uint8_t)regs.rsi))
            ps2_keyboard_stop("keyboard output send failed");
    }
}

void driver_user_main(uint64_t *bootstrap)
{
    uint64_t handles[PS2_KEYBOARD_BOOTSTRAP_CAPS];
    char *cap_names[] = {
        "ps2.port1.tx",
        "ps2.port1.rx",
        "ps2.port2.tx",
        "ps2.port2.rx",
        "ps2.keyboard.output",
        "ps2-keyboard.notify"
    };

    if (!bootstrap || bootstrap[0] != PS2_KEYBOARD_BOOTSTRAP_CAPS)
        ps2_keyboard_stop("invalid bootstrap contract");

    if (sharkix_get_bootstrap(handles, cap_names,
                              PS2_KEYBOARD_BOOTSTRAP_CAPS, bootstrap) !=
        SHARKIX_BOOTSTRAP_OK)
        ps2_keyboard_stop("bootstrap capability lookup failed");

    port1_tx_cap = handles[0];
    port1_rx_cap = handles[1];
    port2_tx_cap = handles[2];
    port2_rx_cap = handles[3];
    output_cap = handles[4];
    ready_cap = handles[5];

    ps2_keyboard_run(port1_tx_cap, port1_rx_cap);
}
