#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "caps.h"
#include "console.h"
#include "ipc.h"
#include "memory.h"
#include "program.h"
#include "startup.h"
#include "thread.h"

enum {
    TESTBIN_CAPS_COUNT = 3,
    TESTBIN_CAPS_STACK_BYTES = (TESTBIN_CAPS_COUNT + 1) * sizeof(uint64_t)
};

extern const uint8_t testbin_caps_image_start[];
extern const uint8_t testbin_caps_image_end[];

static ipc_handle_t endpoint1;
static ipc_handle_t endpoint2;
static ipc_handle_t endpoint3;
static volatile unsigned kernel_worker_done;
static volatile unsigned kernel_worker_failed;

static void kernel_worker(void *argument)
{
    ipc_message_t message = { 0 };

    (void)argument;
    if (ipc_recv(endpoint1, &message) != IPC_OK ||
        message.words[0] != (uint64_t)'A') {
        kernel_worker_failed = 1;
        return;
    }
    console_putc('A');

    message.words[0] = (uint64_t)'B';
    if (ipc_send(thread_current(), endpoint2, &message) != IPC_OK) {
        kernel_worker_failed = 1;
        return;
    }

    if (ipc_recv(endpoint3, &message) != IPC_OK ||
        message.words[0] != (uint64_t)'C') {
        kernel_worker_failed = 1;
        return;
    }
    console_putc('C');
    console_write("\n[testbin_caps] kernel received A and C\n");
    kernel_worker_done = 1;
}

static int create_user_task(address_space_t **out_as,
                            thread_t **out_thread,
                            uint64_t **out_bootstrap)
{
    address_space_t *address_space;
    uintptr_t stack_top;
    uint64_t physical;
    thread_create_params_t params;
    const program_image_t image = {
        .data = testbin_caps_image_start,
        .size = (size_t)(testbin_caps_image_end - testbin_caps_image_start)
    };

    address_space = address_space_create(0);
    if (!address_space ||
        program_map_flat_image(address_space, &image,
                               PROGRAM_DEFAULT_LOAD_ADDRESS) != 0 ||
        program_map_user_stack(address_space, PROGRAM_DEFAULT_STACK_BASE,
                               PAGE_SIZE, &stack_top) != 0)
        return -1;

    physical = address_space_translate(address_space,
                                       stack_top - TESTBIN_CAPS_STACK_BYTES);
    if (physical == UINT64_MAX)
        return -1;

    params = (thread_create_params_t) {
        .entry_rip = PROGRAM_DEFAULT_LOAD_ADDRESS,
        .initial_stack_pointer = stack_top - TESTBIN_CAPS_STACK_BYTES,
        .name = "testbin-caps", .priority = tskIDLE_PRIORITY + 2
    };
    *out_thread = thread_create(address_space, THREAD_PRIVILEGE_USER, &params);
    if (!*out_thread)
        return -1;

    *out_as = address_space;
    *out_bootstrap = (uint64_t *)phys_to_virt(physical);
    return 0;
}

void kernel_startup_profile(void)
{
    address_space_t *user_as = NULL;
    thread_t *user_thread = NULL;
    thread_t *worker_thread;
    uint64_t *bootstrap = NULL;
    cap_handle_t cap1, cap2, cap3;

    if (ipc_create(&endpoint1) != IPC_OK ||
        ipc_create(&endpoint2) != IPC_OK ||
        ipc_create(&endpoint3) != IPC_OK ||
        create_user_task(&user_as, &user_thread, &bootstrap) != 0 ||
        kcap_create(endpoint1, CAP_TYPE_IPC_ENDPOINT, CAP_RIGHT_IPC_SEND,
                    &cap1) != 0 ||
        kcap_create(endpoint2, CAP_TYPE_IPC_ENDPOINT, CAP_RIGHT_IPC_RECV,
                    &cap2) != 0 ||
        kcap_create(endpoint3, CAP_TYPE_IPC_ENDPOINT, CAP_RIGHT_IPC_SEND,
                    &cap3) != 0 ||
        kcapset_addcap(user_as->capset, cap1) != 0 ||
        kcapset_addcap(user_as->capset, cap2) != 0 ||
        kcapset_addcap(user_as->capset, cap3) != 0) {
        console_write("testbin_caps startup failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    bootstrap[0] = TESTBIN_CAPS_COUNT;
    bootstrap[1] = cap1;
    bootstrap[2] = cap2;
    bootstrap[3] = cap3;

    worker_thread = startup_kernel_thread(kernel_worker, "caps-worker",
                                          tskIDLE_PRIORITY + 2);
    if (!worker_thread || thread_start(user_thread) != 0) {
        console_write("testbin_caps thread startup failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    while (!kernel_worker_done && !kernel_worker_failed)
        thread_yield();

    (void)ipc_destroy(endpoint1);
    (void)ipc_destroy(endpoint2);
    (void)ipc_destroy(endpoint3);
    (void)user_as;
    startup_reaper();
}
