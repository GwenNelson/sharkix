// TODO - we really need our own fucking string.h and other freestanding stuff
#include <string.h>

#include "startup.h"
#include "scheduler.h"
#include "thread.h"

#include "console.h"

#include <sharkix/kernel/subsystems/kipc.h>

#include <sharkix/kernel/ipc_registry.h>
#include <sharkix/kernel/kmalloc.h>
#include <sharkix/kernel/arch.h>

#include <libfifo/fifo.h>

#include <stdint.h>
#include <stddef.h>

// basic shell type thing for testing, instead of yet another spinner
// for now, this is going to live in ring0, but later i'll move it all to ring3

static ipc_handle_t console_input_pub;
static ipc_handle_t console_input_sub;

#define INPUT_BUF_CAPACITY 1024

static fifo_t input_buf;
static void** input_buf_storage;

static char   shell_input[INPUT_BUF_CAPACITY]; // this is for reading the actual shell input line
static size_t shell_input_len = 0;
static size_t shell_input_pos = 0;

#define COMMANDS \
	CMD("help",   cmd_help,   "display help output") \
	CMD("uptime", cmd_uptime, "output uptime of the system")

#define CMD(name, func, usage) static void func(void);
COMMANDS
#undef CMD

static char input_buf_getc() {
	return (char)(uintptr_t)fifo_pop_wait(&input_buf);
}

static char* readline() {
	size_t i;

	shell_input_len = 0;
	shell_input_pos = 0;
	shell_input[0] = '\0';

	for (;;) {
		unsigned char c = (unsigned char)input_buf_getc();

		if (c == '\r' || c == '\n') {
			console_putc('\n');
			return shell_input;
		}

		if (c == '\b' || c == 0x7f) {
			if (shell_input_pos == 0)
				continue;

			for (i = shell_input_pos - 1; i < shell_input_len; ++i)
				shell_input[i] = shell_input[i + 1];
			--shell_input_pos;
			--shell_input_len;
			console_putc('\b');
			for (i = shell_input_pos; i < shell_input_len; ++i)
				console_putc(shell_input[i]);
			console_putc(' ');
			for (i = shell_input_pos; i <= shell_input_len; ++i)
				console_putc('\b');
			continue;
		}

		if (c == 0x1b) {
			unsigned char bracket = (unsigned char)input_buf_getc();
			if (bracket == '[') {
				unsigned char key = (unsigned char)input_buf_getc();
				if (key >= '0' && key <= '9') {
					unsigned char terminator = (unsigned char)input_buf_getc();
					if (terminator == '~' && key == '1') {
						while (shell_input_pos > 0) {
							--shell_input_pos;
							console_putc('\b');
						}
					} else if (terminator == '~' && key == '4') {
						while (shell_input_pos < shell_input_len) {
							console_putc(shell_input[shell_input_pos]);
							++shell_input_pos;
						}
					} else if (terminator == '~' && key == '3' &&
					           shell_input_pos < shell_input_len) {
						for (i = shell_input_pos; i < shell_input_len; ++i)
							shell_input[i] = shell_input[i + 1];
						--shell_input_len;
						for (i = shell_input_pos; i < shell_input_len; ++i)
							console_putc(shell_input[i]);
						console_putc(' ');
						for (i = shell_input_pos; i <= shell_input_len; ++i)
							console_putc('\b');
					}
				} else if (key == 'H') {
					while (shell_input_pos > 0) {
						--shell_input_pos;
						console_putc('\b');
					}
				} else if (key == 'F') {
					while (shell_input_pos < shell_input_len) {
						console_putc(shell_input[shell_input_pos]);
						++shell_input_pos;
					}
				} else if (key == 'D' && shell_input_pos > 0) {
					--shell_input_pos;
					console_putc('\b');
				} else if (key == 'C' && shell_input_pos < shell_input_len) {
					console_putc(shell_input[shell_input_pos]);
					++shell_input_pos;
				}
			}
			continue;
		}

		if (c < 0x20 || c == 0x7f)
			continue;

		/* Reserve one byte for the terminating NUL. */
		if (shell_input_len + 1 >= INPUT_BUF_CAPACITY)
			continue;

		for (i = shell_input_len + 1; i > shell_input_pos; --i)
			shell_input[i] = shell_input[i - 1];
		shell_input[shell_input_pos] = (char)c;
		++shell_input_pos;
		++shell_input_len;
		shell_input[shell_input_len] = '\0';

		for (i = shell_input_pos - 1; i < shell_input_len; ++i)
			console_putc(shell_input[i]);
		for (i = shell_input_pos; i < shell_input_len; ++i)
			console_putc('\b');
	}
}

static void input_buf_reader(void* argument) {
	for(;;) {
		// we just sit reading from console.input and shove into our local FIFO in a loop
		ipc_message_t msg = { 0 };
		uint64_t status;
		status = (uint64_t)kipc_recv(console_input_sub,&msg);
	        if(status != IPC_OK) {
			console_write("ERROR! Failed kipc_recv on console.input subscription!\n");
			console_decimal(status);
			arch_halt();
			for(;;);
		}
		// YAY! We got input, shove it into our FIFO
		// TODO: For now, the console drivers only ever feed 1 byte, later we should loop a few times and properly deserialize
		fifo_push_wait(&input_buf, (void *)(uintptr_t)msg.words[1]);
	}
}

static void cmd_uptime() {
	uint64_t total_msecs     = ticks_so_far * (uint64_t)TIMER_TICK_MS; 
	uint64_t total_secs      = total_msecs / 1000;
	uint64_t remaining_msecs = total_msecs % 1000;

	uint64_t days  = total_secs / 86400;
	uint64_t hours = (total_secs % 86400) / 3600;
	uint64_t mins  = (total_secs % 3600)  / 60;
	uint64_t secs  = (total_secs % 60);

	console_write("up ");
	
	console_decimal(days);
	console_write(" days, ");
	
	console_decimal(hours);
	console_write(":");
	console_decimal(mins);
	console_write(":");
	console_decimal(secs);
	console_write(".");
	console_decimal(remaining_msecs);
	
	console_write("\n");

}

static void cmd_help(void) {
	console_write("\n");
	console_write("Supported commands:\n");

#define CMD(name,func,usage) \
	console_write("\t"); \
	console_write(name); \
	console_write("\t\t"); \
	console_write(usage); \
	console_write("\n");
COMMANDS
#undef CMD
}

static void dispatch_cmd(char* cmd) {
	#define CMD(name,func,usage) \
		if(strncmp(cmd, name, strlen(name)) == 0) { \
			func(); \
			return; \
		}

		COMMANDS
	#undef CMD
	console_write("Unknown command: ");
	console_write(cmd);
	console_write("\n");
}

static void normal_task(void *argument) {
	console_write("\n\n\n");

	console_write("Starting sharkix...\n");

	input_buf_storage = kmalloc(sizeof(*input_buf_storage) * INPUT_BUF_CAPACITY);

	if(!input_buf_storage) {
		console_write("ERROR! Could not allocate input buffer!\n");
		arch_halt();
		for(;;);
	}

	if(kipc_registry_lookup("console.input", &console_input_pub) != 0) {
		console_write("ERROR! Could not lookup console.input stream!\n");
		arch_halt();
		for(;;);
	}

	if(kipc_subscribe(console_input_pub, &console_input_sub) != 0) {
		console_write("ERROR! Could not subscribe to console.input stream!\n");
		arch_halt();
		for(;;);
	}

	fifo_init(&input_buf, input_buf_storage, INPUT_BUF_CAPACITY);
	startup_kernel_thread(input_buf_reader,"input-buf-reader", THREAD_PRIORITY_NORMAL);

	console_write("\n");
	for(;;) {
		console_write("Sharkix> ");
		char* cmd = readline();
		dispatch_cmd(cmd);
		
	}
}

void kernel_startup_profile(void) {
    startup_kernel_thread(normal_task, "normal", THREAD_PRIORITY_NORMAL);
}
