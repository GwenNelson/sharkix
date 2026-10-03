#include "startup.h"
#include "thread.h"

#include "console.h"

#include <sharkix/kernel/subsystems/kipc.h>

#include <sharkix/kernel/ipc_registry.h>
#include <sharkix/kernel/kmalloc.h>
#include <sharkix/kernel/arch.h>

#include <libfifo/fifo.h>

// basic shell type thing for testing, instead of yet another spinner
// for now, this is going to live in ring0, but later i'll move it all to ring3

static ipc_handle_t console_input_pub;
static ipc_handle_t console_input_sub;

#define INPUT_BUF_CAPACITY 1024

fifo_t input_buf;
char*  input_buf_storage;

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
		
	}
}

static void normal_task(void *argument) {
	console_write("\n\n\n");

	console_write("Starting sharkix...\n");

	input_buf_storage = kmalloc(sizeof(char) * INPUT_BUF_CAPACITY);

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

	fifo_init(&input_buf,(void**)&input_buf_storage,INPUT_BUF_CAPACITY);
	startup_kernel_thread(input_buf_reader,"input-buf-reader", THREAD_PRIORITY_NORMAL);

	for(;;) {

	}
}

void kernel_startup_profile(void) {
    startup_kernel_thread(normal_task, "normal", THREAD_PRIORITY_NORMAL);
}
