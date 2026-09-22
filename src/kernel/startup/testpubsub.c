#include <stddef.h>
#include <stdint.h>

#include "caps.h"
#include "console.h"
#include "ipc.h"
#include "memory.h"
#include "startup.h"
#include "thread.h"

void kernel_startup_profile(void) {
     for(int x=0; x<10; x++) thread_yield();
     ipc_status_t status;
     ipc_handle_t pub;
     
     ipc_handle_t subA;
     ipc_handle_t subB;
     ipc_handle_t subC;

     console_write("\nTesting publisher creation\n");
     status = ipc_create_publisher(&pub);
     if(status != IPC_OK) {
	console_write("ipc_create_publisher() failed! Error return:");
	console_decimal(status);
	for(;;) thread_yield();
     }

     console_write("Creating subscribers...\n");
     status = ipc_subscribe(pub,&subA);
     status = ipc_subscribe(pub,&subB);
     status = ipc_subscribe(pub,&subC);

     if(status != IPC_OK) {
	console_write("Failed ipc_subscribe()! Error return:");
	console_decimal(status);
	for(;;) thread_yield();
     }

     console_write("Sending a message...\n");
     ipc_message_t first_msg = { 0 };
     first_msg.words[0] = 1337;
     first_msg.words[1] = 69;
     first_msg.words[2] = 42;
     first_msg.words[3] = 666;
     first_msg.words[4] = 1987;

     status = ipc_send(thread_current(),pub, &first_msg);

     if(status != IPC_OK) {
	console_write("Failed ipc_send_nb()! Error return:");
	console_decimal(status);
	for(;;) thread_yield();
     }

     console_write("Testing ABC....\n");

     ipc_message_t a_msg = { 0 };
     status = ipc_recv_nb(subA,&a_msg);
     if(status != IPC_OK) {
	console_write("Failed ipc_recv_nb()! Error return:");
	console_decimal(status);
	for(;;) thread_yield();
     }
     if((a_msg.words[0] == 1337) &&
        (a_msg.words[1] == 69) &&
	(a_msg.words[2] == 42) &&
	(a_msg.words[3] == 666) &&
	(a_msg.words[4] == 1987)) {
	     console_write("A");
     } else {
	console_write("Got the wrong values:\n");
	for(int i=0; i<5; i++) {
            console_decimal(a_msg.words[i]);
	    console_write("\n");
	}
	for(;;) thread_yield();
     }

     ipc_message_t b_msg = { 0 };
     status = ipc_recv_nb(subB,&b_msg);
     if(status != IPC_OK) {
	console_write("Failed ipc_recv_nb()! Error return:");
	console_decimal(status);
	for(;;) thread_yield();
     }
     if((b_msg.words[0] == 1337) &&
        (b_msg.words[1] == 69) &&
	(b_msg.words[2] == 42) &&
	(b_msg.words[3] == 666) &&
	(b_msg.words[4] == 1987)) {
	     console_write("B");
     } else {
	console_write("Got the wrong values:\n");
	for(int i=0; i<5; i++) {
            console_decimal(b_msg.words[i]);
	    console_write("\n");
	}
	for(;;) thread_yield();
     }

 

     ipc_message_t c_msg = { 0 };
     status = ipc_recv_nb(subC,&c_msg);
     if(status != IPC_OK) {
	console_write("Failed ipc_recv_nb()! Error return:");
	console_decimal(status);
	for(;;) thread_yield();
     }
     if((c_msg.words[0] == 1337) &&
        (c_msg.words[1] == 69) &&
	(c_msg.words[2] == 42) &&
	(c_msg.words[3] == 666) &&
	(c_msg.words[4] == 1987)) {
	     console_write("C");
     } else {
	console_write("Got the wrong values:\n");
	for(int i=0; i<5; i++) {
            console_decimal(b_msg.words[i]);
	    console_write("\n");
	}
	for(;;) thread_yield();
     }

 

     console_write("\nIT WORKS!\n");
     for(;;) thread_yield();
 

}
