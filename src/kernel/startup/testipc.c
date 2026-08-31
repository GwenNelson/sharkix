#include "FreeRTOS.h"
#include "startup.h"
#include "thread.h"

#include <sharkix/kernel/ipc.h>
#include <sharkix/kernel/console.h>

#include <libfifo/sync.h>

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
             *
             * Blocking operations still return immediately here because
             * there is nothing valid to block on.
             */
            memset(&message, 0, sizeof(message));

            TEST_CHECK_STATUS(ipc_send(thread, UINT64_MAX, &message),
                              IPC_ERR_NOT_FOUND,
                              "send invalid endpoint");

            TEST_CHECK_STATUS(ipc_recv(thread, UINT64_MAX, &received),
                              IPC_ERR_NOT_FOUND,
                              "recv invalid endpoint");

            /*
             * Empty queue.
             *
             * Normal ipc_recv() would correctly block here forever because
             * this is a single-threaded test, so explicitly use the
             * non-blocking form.
             */
            TEST_CHECK_STATUS(ipc_recv_nb(thread, a, &received),
                              IPC_ERR_CANCELLED,
                              "recv empty queue");

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
             * Normal ipc_send() would block here waiting for space.
             *
             * Use ipc_send_nb() to prove that a full queue does not silently
             * discard or overwrite an existing message.
             */
            memset(&message, 0, sizeof(message));
            message.words[0] = 0xFFFFFFFF;

            TEST_CHECK_STATUS(ipc_send_nb(thread, a, &message),
                              IPC_ERR_CANCELLED,
                              "send to full queue");

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

            TEST_CHECK_STATUS(ipc_recv_nb(thread, a, &received),
                              IPC_ERR_CANCELLED,
                              "queue not empty after drain");

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

            TEST_CHECK_STATUS(ipc_recv_nb(thread, a, &received),
                              IPC_ERR_CANCELLED,
                              "wrap queue not empty");

            /*
             * Destroy an endpoint containing messages. ipc_destroy must
             * dispose of queued allocations safely.
             */
            memset(&message, 0, sizeof(message));

            for (i = 0; i < 16; i++) {
                message.words[0] = i;

                TEST_CHECK_STATUS(ipc_send(thread, b, &message),
                                  IPC_OK,
                                  "populate endpoint before destroy");
            }

            TEST_CHECK_STATUS(ipc_destroy(thread, b), IPC_OK, "destroy populated endpoint");

            TEST_CHECK_STATUS(ipc_send(thread, b, &message),
                              IPC_ERR_NOT_FOUND,
                              "send to destroyed endpoint");

            TEST_CHECK_STATUS(ipc_recv(thread, b, &received),
                              IPC_ERR_NOT_FOUND,
                              "recv from destroyed endpoint");

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

}


static fifo_semaphore_t producer_done_sem;
static fifo_semaphore_t consumer_done_sem;

/*
 * The producer only needs this semaphore because the consumer owns and
 * creates the endpoint. Once the endpoint exists, IPC itself provides all
 * producer/consumer synchronization.
 */
static ipc_handle_t threaded_endpoint;
static fifo_semaphore_t endpoint_ready_sem;

#define THREADED_TEST_MESSAGES 100000


static void test_ipc_producer(void *argument) {
            thread_t *thread;
            ipc_message_t message;
            unsigned int i;

            (void)argument;

            thread = thread_current();

            console_write("IPC producer running, tid=");
            console_decimal(thread->id);
            console_putc('\n');

            /*
             * Consumer creates the endpoint, so don't attempt to use the
             * global handle until it has done so.
             */
            fifo_semaphore_wait(&endpoint_ready_sem);

            for (i = 0; i < THREADED_TEST_MESSAGES; i++) {
                memset(&message, 0, sizeof(message));

                message.words[0] = 0xAAAAAAAAAAAAAAAA;
                message.words[1] = i;
                message.words[2] = i ^ 0x55555555;
                message.words[3] = ~(uint64_t)i;
                message.words[4] = i * 17;

                /*
                 * This should naturally block whenever the consumer has
                 * fallen more than IPC_QUEUE_CAPACITY messages behind.
                 */
                TEST_CHECK_STATUS(ipc_send(thread, threaded_endpoint, &message),
                                  IPC_OK,
                                  "producer send");
            }

            console_write("IPC producer PASSED\n");
	    fifo_semaphore_post(&producer_done_sem);
}


static void test_ipc_consumer(void *argument) {
            thread_t *thread;
            ipc_message_t message;
            ipc_status_t status;
            unsigned int i;

            (void)argument;

            thread = thread_current();

            status = ipc_create(thread, &threaded_endpoint);
            if (status != IPC_OK) {
                console_write("FAIL: threaded endpoint create status=");
                console_decimal(status);
                console_putc('\n');

                for (;;)
                    ;
            }

            console_write("IPC consumer running, tid=");
            console_decimal(thread->id);
            console_putc('\n');

            /*
             * The endpoint is now fully created and published.
             */
            fifo_semaphore_post(&endpoint_ready_sem);

            /*
             * There is intentionally no "producer ready" semaphore anymore.
             *
             * If the producer has not sent anything yet, ipc_recv() should
             * block. If the producer gets ahead, ipc_send() should block once
             * the FIFO fills. That's exactly what we're testing.
             */
	    for (i = 0; i < THREADED_TEST_MESSAGES; i++) {
                TEST_CHECK_STATUS(ipc_recv(thread, threaded_endpoint, &message),
                                  IPC_OK,
                                  "consumer recv");

                TEST_CHECK(message.words[0] == 0xAAAAAAAAAAAAAAAA, "consumer word 0");
                TEST_CHECK(message.words[1] == i, "consumer FIFO order");
                TEST_CHECK(message.words[2] == (i ^ 0x55555555), "consumer word 2");
                TEST_CHECK(message.words[3] == ~(uint64_t)i, "consumer word 3");
                TEST_CHECK(message.words[4] == (i * 17), "consumer word 4");
	    }

            console_write("IPC consumer PASSED\n");

            TEST_CHECK_STATUS(ipc_destroy(thread, threaded_endpoint),
                              IPC_OK,
                              "destroy threaded endpoint");

	    fifo_semaphore_post(&consumer_done_sem);
}


static void test_ipc_threads(void *argument) {
            (void)argument;

            fifo_semaphore_init(&endpoint_ready_sem, 0);
	    fifo_semaphore_init(&producer_done_sem,  0);
	    fifo_semaphore_init(&consumer_done_sem,  0);

            /*
             * Consumer owns the endpoint, so start it first.
             */
            startup_kernel_thread(test_ipc_consumer,
                                  "ipc-consumer",
                                  tskIDLE_PRIORITY + 2);

            startup_kernel_thread(test_ipc_producer,
                                  "ipc-producer",
                                  tskIDLE_PRIORITY + 2);
}

static void test_sharkloop(void* argument) {
}

static void run_tests(void* argument) {
       (void)argument;
	test_single_thread(NULL);
	test_ipc_threads(NULL);
	// at some point we should implement a wait for task or something...
	fifo_semaphore_wait(&producer_done_sem);
	fifo_semaphore_wait(&consumer_done_sem);

	test_sharkloop(NULL);

	// TODO - spawn multiple threads that do a long chain of IPC and ensure it all works
	console_write("ALL PASSED!\n");
}

void kernel_startup_profile(void) {
     ipc_init();

     // we need to be inside a thread to run these things
     startup_kernel_thread(run_tests,"testipc-run_tests",tskIDLE_PRIORITY+2);

}
