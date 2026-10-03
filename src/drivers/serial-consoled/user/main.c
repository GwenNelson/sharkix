#include <stddef.h>
#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

static uint64_t serial_output_cap;
static uint64_t serial_input_cap;
static uint64_t serial_ready_cap;
static uint64_t serial_ier_cap;   // IER (Interrupt Enable) cap
static uint64_t serial_lcr_cap;   // LCR (Line Control) cap
static uint64_t serial_mcr_cap;   // MCR (Modem Control) cap
static uint64_t serial_iir_cap;   // IIR (Interrupt Identify) cap
static uint64_t serial_lsr_cap;   // LSR (Line Status) cap
static uint64_t serial_com1_cap;  // the actual 3f8 port cap

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

static void serial_log(char* msg) {
/*	sharkix_debug_puts("serial:");
	sharkix_debug_puts(msg);
	sharkix_debug_puts("\n");*/
}

static void serial_panic(char* msg) {
	sharkix_debug_puts("PANIC! ");
	sharkix_debug_puts(msg);
	sharkix_debug_puts("Can not continue, terminating...");
	test_exit();
}

static void serial_signal_ready(void) {
	sharkix_syscall_regs_t regs = { 0 };
	// we don't actually care what we send, just need to signal to the kernel we are ready
	// maybe at some point in future we can use this endpoint to signal other stuff too
	// such as error states for example?
	regs.rax = SYSCALL_IPC_SEND;
	regs.rdi = serial_ready_cap;
	sharkix_syscall(&regs);
	if(regs.rax != 0) {
		serial_panic("Failed signalling ready state to kernel via SYSCALL_IPC_SEND!");
	}
}

static void serial_port_outb(uint64_t cap, uint8_t value)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = SYSCALL_PORT_OUTB;
    regs.rdi = cap;
    regs.rsi = 0;
    regs.rdx = value;
    sharkix_syscall(&regs);

    if (regs.rax != 0) {
        serial_panic("serial_port_outb() failed!");
    }
}

static uint8_t serial_port_inb(uint64_t cap)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = SYSCALL_PORT_INB;
    regs.rdi = cap;
    regs.rsi = 0;
    sharkix_syscall(&regs);

    if (regs.rax != 0) {
        serial_panic("serial_port_inb() failed!");
    }

    return (uint8_t)regs.rdx;
}

static void internal_console_serial_putc(char c) {
	// LSR bit 5 indicates that the transmit holding register can accept a byte.
	while((serial_port_inb(serial_lsr_cap) & 0x20) == 0) {}
	if(c == '\n') {
		serial_port_outb(serial_com1_cap,'\r');
	}
	serial_port_outb(serial_com1_cap,c);
}



void run_serial_service(void) {
	// setup the serial port first
	serial_port_outb(serial_ier_cap,0);
	serial_port_outb(serial_lcr_cap,0x80);
	serial_port_outb(serial_com1_cap,3);
	serial_port_outb(serial_ier_cap,0);
	serial_port_outb(serial_lcr_cap,3);
	serial_port_outb(serial_iir_cap,0xc7);
	serial_port_outb(serial_mcr_cap,0x0b);
	
	serial_log("READY!");
	serial_signal_ready();

	sharkix_syscall_regs_t regs = { 0 };
	for (;;) {
            uint64_t words[4];
            uint64_t count;

	    /* Drain available output without blocking input polling. */
	    regs = (sharkix_syscall_regs_t) { 0 };
	    regs.rax = SYSCALL_IPC_TRY_RECV;
	    regs.rdi = serial_output_cap;
            sharkix_syscall(&regs);
	    while (regs.rax == 0) {
	        count = regs.rsi;
	        if (count > 32) count = 32;
	        words[0] = regs.rdx;
	        words[1] = regs.r10;
	        words[2] = regs.r8;
	        words[3] = regs.r9;
	        for (uint64_t i = 0; i < count; ++i)
	            internal_console_serial_putc((char)(words[i / 8] >> ((i % 8) * 8)));
	        regs = (sharkix_syscall_regs_t) { 0 };
	        regs.rax = SYSCALL_IPC_TRY_RECV;
	        regs.rdi = serial_output_cap;
	        sharkix_syscall(&regs);
	    }

	    /* LSR bit 0 says COM1 has a byte in RBR. Drain what is ready now. */
	    while (serial_port_inb(serial_lsr_cap) & 0x01) {
	        uint8_t byte = serial_port_inb(serial_com1_cap);
	        regs = (sharkix_syscall_regs_t) { 0 };
	        regs.rax = SYSCALL_IPC_SEND;
	        regs.rdi = serial_input_cap;
	        regs.rsi = 1;
	        regs.rdx = byte;
	        sharkix_syscall(&regs);
	        if ((int64_t)regs.rax != 0)
	            serial_panic("serial input publication failed!");
    }

	/* This ABI has no userspace yield call; pause until timer preemption. */
	__asm__ volatile ("pause");
        }
	
}

void driver_user_main(uint64_t *bootstrap) {
    uint64_t handles[9];
    char *capv[] = {
        "serial.out",
        "serial.in",
        "serial.ready",
        "serial.ier",
        "serial.lcr",
	"serial.mcr",
	"serial.iir",
	"serial.lsr",
	"serial.com1",
    };

    if (!bootstrap || bootstrap[0] != 9) {
	serial_panic("invalid bootstrap!");
    }

    if (sharkix_get_bootstrap(handles, capv, 9, bootstrap) !=
        SHARKIX_BOOTSTRAP_OK) {
        serial_panic("bootstrap discovery failed\n");
    }

    serial_output_cap = handles[0];
    serial_input_cap  = handles[1];
    serial_ready_cap  = handles[2];
    serial_ier_cap    = handles[3];
    serial_lcr_cap    = handles[4];
    serial_mcr_cap    = handles[5];
    serial_iir_cap    = handles[6];
    serial_lsr_cap    = handles[7];
    serial_com1_cap   = handles[8];

    serial_log("obtained required caps");
    
    run_serial_service();

    test_exit();
}
