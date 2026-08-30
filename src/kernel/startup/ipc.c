#include "FreeRTOS.h"
#include "startup.h"
#include "thread.h"
#include <sharkix/kernel/ipc.h>
#include <sharkix/kernel/console.h>


static void test_ipc_queue_task(void* argument) {
            thread_t *thread;
            ipc_handle_t endpoint;
            ipc_message_t message;
            ipc_message_t received;
            ipc_status_t status;

            console_write("IPC queue test...\n");

            thread = thread_current();

            status = ipc_create(thread, &endpoint);
            if (status != IPC_OK) {
                console_write("ipc_create failed: ");
                console_decimal(status);
                console_putc('\n');
                return;
            }

            console_write("endpoint: ");
            console_hex(endpoint);
            console_putc('\n');

            memset(&message, 0, sizeof(message));
            message.words[0] = 0x1234;
            message.words[1] = 0x5678;

            status = ipc_send(thread, endpoint, &message);
            if (status != IPC_OK) {
                console_write("first ipc_send failed\n");
                return;
            }

            message.words[0] = 0xABCD;
            message.words[1] = 0xEF01;

            status = ipc_send(thread, endpoint, &message);
            if (status != IPC_OK) {
                console_write("second ipc_send failed\n");
                return;
            }

            status = ipc_recv(thread, endpoint, &received);
            if (status != IPC_OK) {
                console_write("first ipc_recv failed\n");
                return;
            }

            console_write("message 1: ");
            console_hex(received.words[0]);
            console_write(" ");
            console_hex(received.words[1]);
            console_putc('\n');

            status = ipc_recv(thread, endpoint, &received);
            if (status != IPC_OK) {
                console_write("second ipc_recv failed\n");
                return;
            }

            console_write("message 2: ");
            console_hex(received.words[0]);
            console_write(" ");
            console_hex(received.words[1]);
            console_putc('\n');

            console_write("IPC queue test complete\n");
}

void kernel_startup_profile(void)
{
	ipc_init();
	startup_kernel_thread(test_ipc_queue_task, "ipctest", tskIDLE_PRIORITY + 1);
}
