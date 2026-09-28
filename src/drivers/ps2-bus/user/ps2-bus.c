#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

enum {
    PS2BUS_BOOTSTRAP_CAPS = 10,
    PS2BUS_IRQ1_BIT = 0,
    PS2BUS_IRQ12_BIT = 1,
    PS2BUS_PORT1_TX_BIT = 2,
    PS2BUS_PORT2_TX_BIT = 3,
    PS2BUS_READY_BIT = 63,
    PS2_STATUS_OUTPUT_FULL = 1u << 0,
    PS2_STATUS_INPUT_FULL = 1u << 1,
    PS2_STATUS_AUX_DATA = 1u << 5,
    PS2_CONFIG_IRQ1 = 1u << 0,
    PS2_CONFIG_IRQ12 = 1u << 1,
    PS2_CONFIG_PORT1_CLOCK_DISABLED = 1u << 4,
    PS2_CONFIG_PORT2_CLOCK_DISABLED = 1u << 5,
    PS2_CONFIG_TRANSLATION = 1u << 6,
    PS2_MAX_WAIT_POLLS = 1000000,
    PS2_MAX_RX_DRAIN = 32,
    IPC_ERR_CANCELLED = -7
};

#define PS2BUS_EVENT_MASK ((UINT64_C(1) << PS2BUS_IRQ1_BIT) | \
                           (UINT64_C(1) << PS2BUS_IRQ12_BIT) | \
                           (UINT64_C(1) << PS2BUS_PORT1_TX_BIT) | \
                           (UINT64_C(1) << PS2BUS_PORT2_TX_BIT))

static uint64_t ps2_irq1_cap;
static uint64_t ps2_irq12_cap;
static uint64_t ps2_pio_data_cap;
static uint64_t ps2_pio_cmd_cap;
static uint64_t ps2_port1_tx_cap;
static uint64_t ps2_port1_rx_cap;
static uint64_t ps2_port2_tx_cap;
static uint64_t ps2_port2_rx_cap;
static uint64_t ps2bus_ready_cap;
static uint64_t ps2bus_events_cap;

static bool ps2_port1_available;
static bool ps2_port2_available;

/* TODO: Candidate for moving/refactoring into libsharkix. */
static uint64_t ps2bus_syscall(uint64_t number,
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

static void ps2bus_log(const char *message)
{
    sharkix_debug_puts("ps2bus: ");
    sharkix_debug_puts(message);
    sharkix_debug_puts("\n");
}

static void ps2bus_exit(void)
{
    (void)ps2bus_syscall(SYSCALL_TEST_EXIT, 0, 0, 0);
    for (;;) {
        __asm__ volatile ("pause");
    }
}

static void ps2bus_panic(const char *message)
{
    ps2bus_log(message);
    ps2bus_exit();
}

static uint8_t ps2bus_port_inb(uint64_t cap)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = SYSCALL_PORT_INB;
    regs.rdi = cap;
    regs.rsi = 0;
    (void)sharkix_syscall(&regs);
    if ((int64_t)regs.rax != 0)
        ps2bus_panic("port input syscall failed");

    return (uint8_t)regs.rdx;
}

static void ps2bus_port_outb(uint64_t cap, uint8_t value)
{
    if ((int64_t)ps2bus_syscall(SYSCALL_PORT_OUTB, cap, 0, value) != 0)
        ps2bus_panic("port output syscall failed");
}

static bool ps2bus_wait_input_empty(void)
{
    uint32_t poll;

    for (poll = 0; poll < PS2_MAX_WAIT_POLLS; ++poll) {
        if (!(ps2bus_port_inb(ps2_pio_cmd_cap) & PS2_STATUS_INPUT_FULL))
            return true;
    }
    return false;
}

static bool ps2bus_wait_output_full(void)
{
    uint32_t poll;

    for (poll = 0; poll < PS2_MAX_WAIT_POLLS; ++poll) {
        if (ps2bus_port_inb(ps2_pio_cmd_cap) & PS2_STATUS_OUTPUT_FULL)
            return true;
    }
    return false;
}

static bool ps2bus_write_command(uint8_t command)
{
    if (!ps2bus_wait_input_empty())
        return false;
    ps2bus_port_outb(ps2_pio_cmd_cap, command);
    return true;
}

static bool ps2bus_write_data(uint8_t data)
{
    if (!ps2bus_wait_input_empty())
        return false;
    ps2bus_port_outb(ps2_pio_data_cap, data);
    return true;
}

static bool ps2bus_read_data(uint8_t *out)
{
    if (!out || !ps2bus_wait_output_full())
        return false;
    *out = ps2bus_port_inb(ps2_pio_data_cap);
    return true;
}

static bool ps2bus_read_config(uint8_t *config)
{
    return ps2bus_write_command(0x20) && ps2bus_read_data(config);
}

static bool ps2bus_write_config(uint8_t config)
{
    return ps2bus_write_command(0x60) && ps2bus_write_data(config);
}

static void ps2bus_flush_output(void)
{
    uint32_t count;

    for (count = 0; count < PS2_MAX_RX_DRAIN; ++count) {
        if (!(ps2bus_port_inb(ps2_pio_cmd_cap) & PS2_STATUS_OUTPUT_FULL))
            break;
        (void)ps2bus_port_inb(ps2_pio_data_cap);
    }
}

static bool ps2bus_test_interface(uint8_t command)
{
    uint8_t result;

    return ps2bus_write_command(command) &&
           ps2bus_read_data(&result) && result == 0;
}

static void ps2bus_initialize_controller(void)
{
    uint8_t config;
    uint8_t result;
    uint8_t port2_config;

    if (!ps2bus_write_command(0xAD) || !ps2bus_write_command(0xA7))
        ps2bus_panic("timed out disabling controller ports");
    ps2bus_flush_output();

    if (!ps2bus_write_command(0xAA) || !ps2bus_read_data(&result) ||
        result != 0x55)
        ps2bus_panic("i8042 controller self-test failed");

    /* Probe for a second controller channel by observing its clock-disable bit. */
    if (!ps2bus_write_command(0xA8) || !ps2bus_read_config(&port2_config))
        ps2bus_panic("failed probing second controller port");
    ps2_port2_available =
        (port2_config & PS2_CONFIG_PORT2_CLOCK_DISABLED) == 0;
    if (!ps2bus_write_command(0xA7))
        ps2bus_panic("timed out disabling second controller port");

    /* Port tests report controller-interface availability, not device type. */
    ps2_port1_available = ps2bus_test_interface(0xAB);
    ps2_port2_available = ps2_port2_available &&
                          ps2bus_test_interface(0xA9);
    ps2bus_flush_output();

    if (!ps2bus_read_config(&config))
        ps2bus_panic("failed reading controller configuration");

    config &= (uint8_t)~(PS2_CONFIG_IRQ1 | PS2_CONFIG_IRQ12 |
                         PS2_CONFIG_TRANSLATION |
                         PS2_CONFIG_PORT1_CLOCK_DISABLED |
                         PS2_CONFIG_PORT2_CLOCK_DISABLED);
    if (ps2_port1_available)
        config |= PS2_CONFIG_IRQ1;
    else
        config |= PS2_CONFIG_PORT1_CLOCK_DISABLED;
    if (ps2_port2_available)
        config |= PS2_CONFIG_IRQ12;
    else
        config |= PS2_CONFIG_PORT2_CLOCK_DISABLED;

    if (!ps2bus_write_config(config))
        ps2bus_panic("failed configuring controller");

    if (ps2_port1_available && !ps2bus_write_command(0xAE))
        ps2bus_panic("failed enabling first controller port");
    if (ps2_port2_available && !ps2bus_write_command(0xA8))
        ps2bus_panic("failed enabling second controller port");

    ps2bus_flush_output();
}

static void ps2bus_signal_lifecycle_ready(void)
{
    if ((int64_t)ps2bus_syscall(SYSCALL_NOTIFY_SIGNAL,
                                ps2bus_ready_cap,
                                UINT64_C(1) << PS2BUS_READY_BIT,
                                0) != 0)
        ps2bus_panic("failed signalling lifecycle readiness");
}

static void ps2bus_notify_ack(uint64_t bits)
{
    if ((int64_t)ps2bus_syscall(SYSCALL_NOTIFY_ACK,
                                ps2bus_events_cap, bits, 0) != 0)
        ps2bus_panic("runtime notification acknowledgement failed");
}

static void ps2bus_irq_ack(uint64_t irq_cap)
{
    if ((int64_t)ps2bus_syscall(SYSCALL_IRQ_ACK, irq_cap, 0, 0) != 0)
        ps2bus_panic("IRQ acknowledgement failed");
}

static void ps2bus_send_byte(uint64_t endpoint_cap, uint8_t byte)
{
    /* Normal IPC_SEND may block; this is the sole blocking IPC operation. */
    if ((int64_t)ps2bus_syscall(SYSCALL_IPC_SEND,
                                endpoint_cap, byte, 0) != 0)
        ps2bus_panic("failed forwarding PS/2 byte");
}

static void ps2bus_service_hardware(void)
{
    uint32_t count;

    for (count = 0; count < PS2_MAX_RX_DRAIN; ++count) {
        uint8_t status = ps2bus_port_inb(ps2_pio_cmd_cap);
        uint8_t byte;

        if (!(status & PS2_STATUS_OUTPUT_FULL))
            break;

        byte = ps2bus_port_inb(ps2_pio_data_cap);
        if (status & PS2_STATUS_AUX_DATA)
            ps2bus_send_byte(ps2_port2_rx_cap, byte);
        else
            ps2bus_send_byte(ps2_port1_rx_cap, byte);
    }
}

static bool ps2bus_send_device_byte(bool port2, uint8_t byte)
{
    if (port2) {
        if (!ps2bus_write_command(0xD4))
            return false;
    }

    if (!ps2bus_wait_input_empty())
        return false;
    ps2bus_port_outb(ps2_pio_data_cap, byte);
    return true;
}

static void ps2bus_drain_tx(uint64_t endpoint_cap, bool port2)
{
    for (;;) {
        sharkix_syscall_regs_t regs = { 0 };

        regs.rax = SYSCALL_IPC_TRY_RECV;
        regs.rdi = endpoint_cap;
        (void)sharkix_syscall(&regs);

        if ((int64_t)regs.rax == IPC_ERR_CANCELLED)
            return;
        if ((int64_t)regs.rax != 0)
            ps2bus_panic("nonblocking receive failed");

        if (!ps2bus_send_device_byte(port2, (uint8_t)regs.rsi))
            ps2bus_log("controller stayed busy; dropping one TX byte");
    }
}

static void ps2bus_event_loop(void)
{
    for (;;) {
        sharkix_syscall_regs_t regs = { 0 };
        uint64_t pending;

        regs.rax = SYSCALL_NOTIFY_WAIT;
        regs.rdi = ps2bus_events_cap;
        regs.rsi = PS2BUS_EVENT_MASK;
        (void)sharkix_syscall(&regs);
        if ((int64_t)regs.rax != 0)
            ps2bus_panic("runtime notification wait failed");

        regs = (sharkix_syscall_regs_t) { 0 };
        regs.rax = SYSCALL_NOTIFY_POLL;
        regs.rdi = ps2bus_events_cap;
        (void)sharkix_syscall(&regs);
        if ((int64_t)regs.rax != 0)
            ps2bus_panic("runtime notification poll failed");
        pending = regs.rdx & PS2BUS_EVENT_MASK;

        if (pending & (UINT64_C(1) << PS2BUS_IRQ1_BIT)) {
            ps2bus_notify_ack(UINT64_C(1) << PS2BUS_IRQ1_BIT);
            ps2bus_service_hardware();
            ps2bus_irq_ack(ps2_irq1_cap);
        }

        if (pending & (UINT64_C(1) << PS2BUS_IRQ12_BIT)) {
            ps2bus_notify_ack(UINT64_C(1) << PS2BUS_IRQ12_BIT);
            ps2bus_service_hardware();
            ps2bus_irq_ack(ps2_irq12_cap);
        }

        if (pending & (UINT64_C(1) << PS2BUS_PORT1_TX_BIT)) {
            ps2bus_notify_ack(UINT64_C(1) << PS2BUS_PORT1_TX_BIT);
            ps2bus_drain_tx(ps2_port1_tx_cap, false);
        }

        if (pending & (UINT64_C(1) << PS2BUS_PORT2_TX_BIT)) {
            ps2bus_notify_ack(UINT64_C(1) << PS2BUS_PORT2_TX_BIT);
            ps2bus_drain_tx(ps2_port2_tx_cap, true);
        }
    }
}

void driver_user_main(uint64_t *bootstrap)
{
    uint64_t handles[PS2BUS_BOOTSTRAP_CAPS];
    char *cap_names[] = {
        "ps2bus.irq.1",
        "ps2bus.irq.12",
        "ps2bus.pio.data",
        "ps2bus.pio.cmd",
        "ps2.port1.tx",
        "ps2.port1.rx",
        "ps2.port2.tx",
        "ps2.port2.rx",
        "ps2bus.notify",
        "ps2bus.events"
    };

    if (!bootstrap || bootstrap[0] != PS2BUS_BOOTSTRAP_CAPS)
        ps2bus_panic("invalid bootstrap contract");

    if (sharkix_get_bootstrap(handles, cap_names, PS2BUS_BOOTSTRAP_CAPS,
                              bootstrap) != SHARKIX_BOOTSTRAP_OK)
        ps2bus_panic("bootstrap capability lookup failed");

    ps2_irq1_cap = handles[0];
    ps2_irq12_cap = handles[1];
    ps2_pio_data_cap = handles[2];
    ps2_pio_cmd_cap = handles[3];
    ps2_port1_tx_cap = handles[4];
    ps2_port1_rx_cap = handles[5];
    ps2_port2_tx_cap = handles[6];
    ps2_port2_rx_cap = handles[7];
    ps2bus_ready_cap = handles[8];
    ps2bus_events_cap = handles[9];

    ps2bus_initialize_controller();
    ps2bus_signal_lifecycle_ready();
    ps2bus_log("controller ready; entering event loop");
    ps2bus_event_loop();
}
