#include "FreeRTOS.h"
#include "startup.h"
#include "thread.h"
#include <sharkix/kernel/ipc.h>
#include <sharkix/kernel/console.h>

#include <string.h>

#define TEST_CHECK(condition, message) do { \
    if (!(condition)) { \
        console_write("FAIL: " message "\n"); \
        return; \
    } \
} while (0)

#define TEST_CHECK_STATUS(expression, expected, message) do { \
    ipc_status_t _status = (expression); \
    if (_status != (expected)) { \
        console_write("FAIL: " message " status="); \
        console_decimal(_status); \
        console_putc('\n'); \
        return; \
    } \
} while (0)

static void test_single_thread(void *argument) {
            thread_t *thread;
            ipc_handle_t a;
            ipc_handle_t b;
            ipc_handle_t c;
            ipc_handle_t d;
            ipc_message_t message;
            ipc_message_t received;
            unsigned int i;

            (void)argument;

            console_write("IPC single-thread tests...\n");

            thread = thread_current();
            TEST_CHECK(thread != NULL, "thread_current returned NULL");

            /*
             * Endpoint creation and monotonically increasing handles.
             */
            TEST_CHECK_STATUS(ipc_create(thread, &a), IPC_OK, "create A");
            TEST_CHECK_STATUS(ipc_create(thread, &b), IPC_OK, "create B");
            TEST_CHECK_STATUS(ipc_create(thread, &c), IPC_OK, "create C");

            TEST_CHECK(a != 0, "endpoint A is zero");
            TEST_CHECK(b != 0, "endpoint B is zero");
            TEST_CHECK(c != 0, "endpoint C is zero");

            TEST_CHECK(a < b, "endpoint B not after A");
            TEST_CHECK(b < c, "endpoint C not after B");

            /*
             * Invalid endpoint.
             */
            memset(&message, 0, sizeof(message));
            TEST_CHECK_STATUS(ipc_send(thread, UINT64_MAX, &message), IPC_ERR_NOT_FOUND, "send invalid endpoint");
            TEST_CHECK_STATUS(ipc_recv(thread, UINT64_MAX, &received), IPC_ERR_NOT_FOUND, "recv invalid endpoint");

            /*
             * Empty queue.
             */
            TEST_CHECK_STATUS(ipc_recv(thread, a, &received), IPC_ERR_CANCELLED, "recv empty queue");

            /*
             * Exercise all five message words and sender metadata.
             */
            memset(&message, 0, sizeof(message));

            message.words[0] = 0x1111111111111111;
            message.words[1] = 0x2222222222222222;
            message.words[2] = 0x3333333333333333;
            message.words[3] = 0x4444444444444444;
            message.words[4] = 0x5555555555555555;

            TEST_CHECK_STATUS(ipc_send(thread, a, &message), IPC_OK, "send five-word message");
            TEST_CHECK_STATUS(ipc_recv(thread, a, &received), IPC_OK, "recv five-word message");

            TEST_CHECK(received.words[0] == 0x1111111111111111, "word 0 corrupted");
            TEST_CHECK(received.words[1] == 0x2222222222222222, "word 1 corrupted");
            TEST_CHECK(received.words[2] == 0x3333333333333333, "word 2 corrupted");
            TEST_CHECK(received.words[3] == 0x4444444444444444, "word 3 corrupted");
            TEST_CHECK(received.words[4] == 0x5555555555555555, "word 4 corrupted");
            TEST_CHECK(received.sender_tid == thread->id, "sender TID incorrect");

            /*
             * Make sure ipc_send copied the message rather than retaining
             * a pointer to the caller's buffer.
             */
            memset(&message, 0, sizeof(message));
            message.words[0] = 0xDEADBEEF;

            TEST_CHECK_STATUS(ipc_send(thread, a, &message), IPC_OK, "send copy test");

            message.words[0] = 0xBADBADBAD;

            TEST_CHECK_STATUS(ipc_recv(thread, a, &received), IPC_OK, "recv copy test");
            TEST_CHECK(received.words[0] == 0xDEADBEEF, "message was not copied");

            /*
             * Interleaved endpoints. This checks that endpoint lookup and
             * queue state are genuinely independent.
             */
            memset(&message, 0, sizeof(message));

            message.words[0] = 0xA1;
            TEST_CHECK_STATUS(ipc_send(thread, a, &message), IPC_OK, "send A1");

            message.words[0] = 0xB1;
            TEST_CHECK_STATUS(ipc_send(thread, b, &message), IPC_OK, "send B1");

            message.words[0] = 0xA2;
            TEST_CHECK_STATUS(ipc_send(thread, a, &message), IPC_OK, "send A2");

            message.words[0] = 0xC1;
            TEST_CHECK_STATUS(ipc_send(thread, c, &message), IPC_OK, "send C1");

            message.words[0] = 0xB2;
            TEST_CHECK_STATUS(ipc_send(thread, b, &message), IPC_OK, "send B2");

            TEST_CHECK_STATUS(ipc_recv(thread, b, &received), IPC_OK, "recv B1");
            TEST_CHECK(received.words[0] == 0xB1, "B1 wrong");

            TEST_CHECK_STATUS(ipc_recv(thread, a, &received), IPC_OK, "recv A1");
            TEST_CHECK(received.words[0] == 0xA1, "A1 wrong");

            TEST_CHECK_STATUS(ipc_recv(thread, c, &received), IPC_OK, "recv C1");
            TEST_CHECK(received.words[0] == 0xC1, "C1 wrong");

            TEST_CHECK_STATUS(ipc_recv(thread, a, &received), IPC_OK, "recv A2");
            TEST_CHECK(received.words[0] == 0xA2, "A2 wrong");

            TEST_CHECK_STATUS(ipc_recv(thread, b, &received), IPC_OK, "recv B2");
            TEST_CHECK(received.words[0] == 0xB2, "B2 wrong");

            /*
             * Fill an entire endpoint queue.
             */
            for (i = 0; i < IPC_QUEUE_CAPACITY; i++) {
                memset(&message, 0, sizeof(message));

                message.words[0] = i;
                message.words[1] = i ^ 0x55555555;
                message.words[2] = i + 0x1000;
                message.words[3] = ~(uint64_t)i;
                message.words[4] = i * 17;

                TEST_CHECK_STATUS(ipc_send(thread, a, &message), IPC_OK, "fill queue");
            }

            /*
             * The 65th message must not silently disappear or overwrite
             * an existing message.
             */
            memset(&message, 0, sizeof(message));
            message.words[0] = 0xFFFFFFFF;

            TEST_CHECK_STATUS(ipc_send(thread, a, &message), IPC_ERR_CANCELLED, "send to full queue");

            /*
             * Drain it and prove FIFO ordering plus complete payload
             * integrity for every entry.
             */
            for (i = 0; i < IPC_QUEUE_CAPACITY; i++) {
                TEST_CHECK_STATUS(ipc_recv(thread, a, &received), IPC_OK, "drain queue");

                TEST_CHECK(received.words[0] == i, "queue FIFO order corrupted");
                TEST_CHECK(received.words[1] == (i ^ 0x55555555), "queue word 1 corrupted");
                TEST_CHECK(received.words[2] == (i + 0x1000), "queue word 2 corrupted");
                TEST_CHECK(received.words[3] == ~(uint64_t)i, "queue word 3 corrupted");
                TEST_CHECK(received.words[4] == (i * 17), "queue word 4 corrupted");
                TEST_CHECK(received.sender_tid == thread->id, "queue sender TID corrupted");
            }

            TEST_CHECK_STATUS(ipc_recv(thread, a, &received), IPC_ERR_CANCELLED, "queue not empty after drain");

            /*
             * Wrap the FIFO head/tail around several times without ever
             * completely draining it between rounds.
             */
            for (i = 0; i < IPC_QUEUE_CAPACITY / 2; i++) {
                memset(&message, 0, sizeof(message));
                message.words[0] = i;
                TEST_CHECK_STATUS(ipc_send(thread, a, &message), IPC_OK, "wrap initial fill");
            }

            for (i = 0; i < 512; i++) {
                TEST_CHECK_STATUS(ipc_recv(thread, a, &received), IPC_OK, "wrap recv");
                TEST_CHECK(received.words[0] == i, "wrap FIFO order corrupted");

                memset(&message, 0, sizeof(message));
                message.words[0] = i + (IPC_QUEUE_CAPACITY / 2);
                TEST_CHECK_STATUS(ipc_send(thread, a, &message), IPC_OK, "wrap send");
            }

            for (i = 512; i < 512 + (IPC_QUEUE_CAPACITY / 2); i++) {
                TEST_CHECK_STATUS(ipc_recv(thread, a, &received), IPC_OK, "wrap final drain");
                TEST_CHECK(received.words[0] == i, "wrap final order corrupted");
            }

            TEST_CHECK_STATUS(ipc_recv(thread, a, &received), IPC_ERR_CANCELLED, "wrap queue not empty");

            /*
             * Destroy an endpoint containing messages. ipc_destroy must
             * dispose of queued allocations safely.
             */
            memset(&message, 0, sizeof(message));

            for (i = 0; i < 16; i++) {
                message.words[0] = i;
                TEST_CHECK_STATUS(ipc_send(thread, b, &message), IPC_OK, "populate endpoint before destroy");
            }

            TEST_CHECK_STATUS(ipc_destroy(thread, b), IPC_OK, "destroy populated endpoint");

            TEST_CHECK_STATUS(ipc_send(thread, b, &message), IPC_ERR_NOT_FOUND, "send to destroyed endpoint");
            TEST_CHECK_STATUS(ipc_recv(thread, b, &received), IPC_ERR_NOT_FOUND, "recv from destroyed endpoint");

            /*
             * Handles must never be reused.
             */
            TEST_CHECK_STATUS(ipc_create(thread, &d), IPC_OK, "create after destroy");
            TEST_CHECK(d > c, "destroyed endpoint handle was reused");
            TEST_CHECK(d != b, "destroyed handle reused");

            /*
             * Repeated create/destroy. This catches a surprising amount
             * of hash-table and allocation stupidity.
             */
            for (i = 0; i < 256; i++) {
                ipc_handle_t temporary;

                TEST_CHECK_STATUS(ipc_create(thread, &temporary), IPC_OK, "repeated create");
                TEST_CHECK(temporary > d, "handles stopped increasing");

                memset(&message, 0, sizeof(message));
                message.words[0] = i;

                TEST_CHECK_STATUS(ipc_send(thread, temporary, &message), IPC_OK, "repeated send");
                TEST_CHECK_STATUS(ipc_recv(thread, temporary, &received), IPC_OK, "repeated recv");
                TEST_CHECK(received.words[0] == i, "repeated message corrupted");

                TEST_CHECK_STATUS(ipc_destroy(thread, temporary), IPC_OK, "repeated destroy");

                d = temporary;
            }

            /*
             * Original surviving endpoints should still work after all
             * that hash-table churn.
             */
            memset(&message, 0, sizeof(message));
            message.words[0] = 0xAAAAAAAAAAAAAAAA;

            TEST_CHECK_STATUS(ipc_send(thread, a, &message), IPC_OK, "send A after churn");
            TEST_CHECK_STATUS(ipc_recv(thread, a, &received), IPC_OK, "recv A after churn");
            TEST_CHECK(received.words[0] == 0xAAAAAAAAAAAAAAAA, "A corrupted after churn");

            TEST_CHECK_STATUS(ipc_send(thread, c, &message), IPC_OK, "send C after churn");
            TEST_CHECK_STATUS(ipc_recv(thread, c, &received), IPC_OK, "recv C after churn");
            TEST_CHECK(received.words[0] == 0xAAAAAAAAAAAAAAAA, "C corrupted after churn");

            TEST_CHECK_STATUS(ipc_destroy(thread, a), IPC_OK, "destroy A");
            TEST_CHECK_STATUS(ipc_destroy(thread, c), IPC_OK, "destroy C");

            console_write("IPC single-thread tests PASSED\n");

            for (;;)
                ;
}

/*
 * For now this is deliberately just a scheduler/thread smoke test.
 *
 * Once IPC blocking semantics exist, these are the obvious place to
 * turn into a sender and receiver pair.
 */
static void test_thread_a(void *argument) {
            (void)argument;

            console_write("IPC thread A running, tid=");
            console_decimal(thread_current()->id);
            console_putc('\n');

            for (;;)
                ;
}

static void test_thread_b(void *argument) {
            (void)argument;

            console_write("IPC thread B running, tid=");
            console_decimal(thread_current()->id);
            console_putc('\n');

            for (;;)
                ;
}

void kernel_startup_profile(void)
{
    ipc_init();

    startup_kernel_thread(test_single_thread, "ipctest", tskIDLE_PRIORITY + 2);

    startup_kernel_thread(test_thread_a, "ipcthread-a", tskIDLE_PRIORITY + 1);
    startup_kernel_thread(test_thread_b, "ipcthread-b", tskIDLE_PRIORITY + 1);
}
