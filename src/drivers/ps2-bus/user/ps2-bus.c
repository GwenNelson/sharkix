#include <stddef.h>
#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

static uint64_t ps2_irq1_cap;
static uint64_t ps2_irq12_cap;

static uint64_t ps2_pio_data_cap;
static uint64_t ps2_pio_cmd_cap;

static uint64_t ps2_port1_ep_cap;
static uint64_t ps2_port2_ep_cap;

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
	sharkix_debug_puts("ps2bus-consoled:");
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

static void internal_console_ps2bus_putc(char c) {
	// LSR bit 5 indicates that the transmit holding register can accept a byte.
	while((ps2bus_port_inb(ps2bus_lsr_cap) & 0x20) == 0) {}
	if(c == '\n') {
		ps2bus_port_outb(ps2bus_com1_cap,'\r');
	}
	ps2bus_port_outb(ps2bus_com1_cap,c);
}



void run_ps2bus_service(void) {
	// setup the ps2bus port first
	ps2bus_port_outb(ps2bus_ier_cap,0);
	ps2bus_port_outb(ps2bus_lcr_cap,0x80);
	ps2bus_port_outb(ps2bus_com1_cap,3);
	ps2bus_port_outb(ps2bus_ier_cap,0);
	ps2bus_port_outb(ps2bus_lcr_cap,3);
	ps2bus_port_outb(ps2bus_iir_cap,0xc7);
	ps2bus_port_outb(ps2bus_mcr_cap,0x0b);
	
	ps2bus_log("READY!");
	ps2bus_signal_ready();

	sharkix_syscall_regs_t regs = { 0 };
        regs.rax = SYSCALL_IPC_RECV;
        regs.rdi = ps2bus_output_cap;
        for (;;) {
            uint64_t words[4];
            uint64_t count;

            sharkix_syscall(&regs);
            if (regs.rax != 0) {   /* loop until we get an actual message */
                regs.rax = SYSCALL_IPC_RECV;
                regs.rdi = ps2bus_output_cap;
                continue;
            }
            count = regs.rsi;
            if (count > 32) count = 32;
            words[0] = regs.rdx;
            words[1] = regs.r10;
            words[2] = regs.r8;
            words[3] = regs.r9;
            for (uint64_t i = 0; i < count; ++i)
                 internal_console_ps2bus_putc((char)(words[i / 8] >> ((i % 8) * 8)));
            regs.rax = SYSCALL_IPC_RECV;
            regs.rdi = ps2bus_output_cap;
        }
	
}

void driver_user_main(uint64_t *bootstrap) {
    uint64_t handles[8];
    char *capv[] = {
        "ps2bus.out",
        "ps2bus.ready",
        "ps2bus.ier",
        "ps2bus.lcr",
	"ps2bus.mcr",
	"ps2bus.iir",
	"ps2bus.lsr",
	"ps2bus.com1",
    };

    if (!bootstrap || bootstrap[0] != 8) {
	ps2bus_panic("invalid bootstrap!");
    }

    if (sharkix_get_bootstrap(handles, capv, 8, bootstrap) !=
        SHARKIX_BOOTSTRAP_OK) {
        ps2bus_panic("bootstrap discovery failed\n");
    }

    ps2bus_output_cap = handles[0];
    ps2bus_ready_cap  = handles[1];
    ps2bus_ier_cap    = handles[2];
    ps2bus_lcr_cap    = handles[3];
    ps2bus_mcr_cap    = handles[4];
    ps2bus_iir_cap    = handles[5];
    ps2bus_lsr_cap    = handles[6];
    ps2bus_com1_cap   = handles[7];

    ps2bus_log("obtained required caps");
    
    run_ps2bus_service();

    test_exit();
}
