#include <stddef.h>
#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

static uint64_t ps2_irq1_cap;

static uint64_t ps2_pio_data_cap;
static uint64_t ps2_pio_cmd_cap;

static uint64_t ps2_port1_ep_cap;
static uint64_t ps2bus_ready_cap;

#define PS2_STATUS_OUTPUT_FULL  (1u << 0)
#define PS2_STATUS_INPUT_FULL   (1u << 1)

static uint64_t syscall_call(uint64_t number, uint64_t arg0, uint64_t arg1)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = number;
    regs.rdi = arg0;
    regs.rsi = arg1;
    (void)sharkix_syscall(&regs);
    return regs.rax;
}

static void test_exit(void)
{
    (void)syscall_call(SYSCALL_TEST_EXIT, 0, 0);
    for (;;) {
        __asm__ volatile ("pause");
    }
}

static void ps2bus_log(char* msg) {
	sharkix_debug_puts("ps2bus:");
	sharkix_debug_puts(msg);
	sharkix_debug_puts("\n");
}

static void ps2bus_panic(char* msg) {
	ps2bus_log("PANIC!");
	ps2bus_log(msg);
	ps2bus_log("Can not continue, terminating...");
	test_exit();
}

static void ps2bus_signal_ready(void) {
	sharkix_syscall_regs_t regs = { 0 };
	// we don't actually care what we send, just need to signal to the kernel we are ready
	// maybe at some point in future we can use this endpoint to signal other stuff too
	// such as error states for example?
	regs.rax = SYSCALL_IPC_SEND;
	regs.rdi = ps2bus_ready_cap;
	sharkix_syscall(&regs);
	if(regs.rax != 0) {
		ps2bus_panic("Failed signalling ready state to kernel via SYSCALL_IPC_SEND!");
	}
}

static void ps2bus_port_outb(uint64_t cap, uint8_t value)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = SYSCALL_PORT_OUTB;
    regs.rdi = cap;
    regs.rsi = 0;
    regs.rdx = value;
    sharkix_syscall(&regs);

    if (regs.rax != 0) {
        ps2bus_panic("ps2bus_port_outb() failed!");
    }
}

static uint8_t ps2bus_port_inb(uint64_t cap)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = SYSCALL_PORT_INB;
    regs.rdi = cap;
    regs.rsi = 0;
    sharkix_syscall(&regs);

    if (regs.rax != 0) {
        ps2bus_panic("ps2bus_port_inb() failed!");
    }

    return (uint8_t)regs.rdx;
}

static void ps2_wait_read(void) {
	while (!(ps2bus_port_inb(ps2_pio_cmd_cap) & PS2_STATUS_OUTPUT_FULL));
}

static void ps2_wait_write(void) {
	while (ps2bus_port_inb(ps2_pio_cmd_cap) & PS2_STATUS_INPUT_FULL);
}

static void ps2_wait_irq1(void) {
	sharkix_syscall_regs_t regs = { 0 };
	regs.rax = SYSCALL_IRQ_WAIT;
	regs.rdi = ps2_irq1_cap;
	sharkix_syscall(&regs);
	if(regs.rax != 0) ps2bus_panic("ps2_wait_irq1() failed!");
}

static void ps2_ack_irq1(void) {
	sharkix_syscall_regs_t regs = { 0 };
	regs.rax = SYSCALL_IRQ_ACK;
	regs.rdi = ps2_irq1_cap;
	sharkix_syscall(&regs);
	if(regs.rax != 0) ps2bus_panic("ps2_ack_irq1() failed!");
}

static void ps2_send_scancode(uint8_t scan_code) {
	sharkix_syscall_regs_t regs = { 0 };
	regs.rax = SYSCALL_IPC_SEND;
	regs.rdi = ps2_port1_ep_cap;
	regs.rsi = (uint64_t)scan_code;
	sharkix_syscall(&regs);
	if(regs.rax != 0) {
		ps2bus_panic("Failed IPC!");
	}
}

void run_ps2bus_service(void) {
	ps2bus_log("Disable port1");
	// first, disable port1 (so we can reconfigure everything)
	ps2_wait_write();
	ps2bus_port_outb(ps2_pio_cmd_cap,0xAD);

	ps2bus_log("Disable port2");
	// turn off port2 too for now
	ps2_wait_write();
	ps2bus_port_outb(ps2_pio_cmd_cap,0xA7);

	ps2bus_log("Consume stuck input");
	while (ps2bus_port_inb(ps2_pio_cmd_cap) & PS2_STATUS_OUTPUT_FULL) {
		(void)ps2bus_port_inb(ps2_pio_data_cap);
	}

	ps2bus_log("Configure controller");
	ps2_wait_write();
	ps2bus_port_outb(ps2_pio_cmd_cap, 0x20); // read config byte
	ps2_wait_read();
	uint8_t config = ps2bus_port_inb(ps2_pio_data_cap);

	config |=  (1 << 0); // IRQ1 enable
	config &= ~(1 << 4); // port1 clock enable

	ps2_wait_write();
	ps2bus_port_outb(ps2_pio_cmd_cap,0x60); // write config byte
	ps2_wait_write();
	ps2bus_port_outb(ps2_pio_data_cap,config);

	ps2bus_log("Enable port1");

	ps2_wait_write();
	ps2bus_port_outb(ps2_pio_cmd_cap,0xAE);
	
	ps2bus_log("READY!");
	ps2bus_signal_ready();

	for(;;) {
		ps2_wait_irq1();
		ps2_wait_read();
		uint8_t scancode = ps2bus_port_inb(ps2_pio_data_cap);
		ps2_send_scancode(scancode);
		ps2_ack_irq1();
	}
}


static uint64_t ps2_irq1_cap;

static uint64_t ps2_pio_data_cap;
static uint64_t ps2_pio_cmd_cap;

static uint64_t ps2_port1_ep_cap;
void driver_user_main(uint64_t *bootstrap) {
    uint64_t handles[5];
    char *capv[] = {
        "ps2bus.irq.1",
	"ps2bus.pio.data",
	"ps2bus.pio.cmd",
	"ps2bus.port1",
	"ps2bus.ready",
    };

    if (!bootstrap || bootstrap[0] != 5) {
	ps2bus_panic("invalid bootstrap!");
    }

    if (sharkix_get_bootstrap(handles, capv, 5, bootstrap) !=
        SHARKIX_BOOTSTRAP_OK) {
        ps2bus_panic("bootstrap discovery failed\n");
    }

    ps2_irq1_cap     = handles[0];
    ps2_pio_data_cap = handles[1];
    ps2_pio_cmd_cap  = handles[2];
    ps2_port1_ep_cap = handles[3];
    ps2bus_ready_cap = handles[4];

    ps2bus_log("obtained required caps");
    
    run_ps2bus_service();

    test_exit();
}
