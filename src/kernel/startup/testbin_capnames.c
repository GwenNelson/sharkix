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
    TESTBIN_CAPNAMES_STACK_WORDS = 4,
    TESTBIN_CAPNAMES_STACK_BYTES = TESTBIN_CAPNAMES_STACK_WORDS * sizeof(uint64_t)
};

extern const uint8_t testbin_capnames_image_start[];
extern const uint8_t testbin_capnames_image_end[];

static ipc_handle_t endpoint;
static cap_handle_t user_cap;
static volatile unsigned kernel_worker_done;
static volatile unsigned kernel_worker_failed;

static void kernel_worker(void *argument)
{
    ipc_message_t message = { 0 };
    char name[KCAP_NAME_MAX];
    size_t name_len = 0;
    static const char renamed[] = "renamed-cap";

    (void)argument;
    if (ipc_recv(endpoint, &message) != IPC_OK || message.words[0] != 42 ||
        kcap_get_name(user_cap, name, sizeof(name), &name_len) != 0 ||
        name_len != sizeof(renamed) - 1 ||
        memcmp(name, renamed, name_len) != 0) {
        kernel_worker_failed = 1;
        return;
    }

    console_decimal(42);
    console_write("\n[testbin_capnames] cap renamed to renamed-cap\n");
    kernel_worker_done = 1;
}

static int create_user_task(address_space_t **out_as, thread_t **out_thread,
                            uint64_t **out_bootstrap)
{
    address_space_t *address_space;
    uintptr_t stack_top;
    uint64_t physical;
    thread_create_params_t params;
    const program_image_t image = {
        .data = testbin_capnames_image_start,
        .size = (size_t)(testbin_capnames_image_end - testbin_capnames_image_start)
    };

    address_space = address_space_create(0);
    if (!address_space ||
        program_map_flat_image(address_space, &image,
                               PROGRAM_DEFAULT_LOAD_ADDRESS) != 0 ||
        program_map_user_stack(address_space, PROGRAM_DEFAULT_STACK_BASE,
                               PAGE_SIZE, &stack_top) != 0)
        return -1;

    physical = address_space_translate(address_space,
                                       stack_top - TESTBIN_CAPNAMES_STACK_BYTES);
    if (physical == UINT64_MAX)
        return -1;

    params = (thread_create_params_t) {
        .entry_rip = PROGRAM_DEFAULT_LOAD_ADDRESS,
        .initial_stack_pointer = stack_top - TESTBIN_CAPNAMES_STACK_BYTES,
        .name = "testbin-capnames", .priority = tskIDLE_PRIORITY + 2
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
    static const char initial_name[] = "hello-cap";

    if (ipc_create(&endpoint) != IPC_OK ||
        kcap_create(endpoint, CAP_TYPE_IPC_ENDPOINT,
                    CAP_RIGHT_IPC_SEND | CAP_RIGHT_GETNAME | CAP_RIGHT_SETNAME,
                    &user_cap) != 0 ||
        kcap_set_name(user_cap, initial_name, sizeof(initial_name) - 1) != 0 ||
        create_user_task(&user_as, &user_thread, &bootstrap) != 0 ||
        kcapset_addcap(user_as->capset, user_cap) != 0) {
        console_write("testbin_capnames startup failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    bootstrap[0] = 1;
    bootstrap[1] = user_cap;
    bootstrap[2] = 0;
    bootstrap[3] = 0;

    worker_thread = startup_kernel_thread(kernel_worker, "capnames-worker",
                                          tskIDLE_PRIORITY + 2);
    if (!worker_thread || thread_start(user_thread) != 0) {
        console_write("testbin_capnames thread startup failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    while (!kernel_worker_done && !kernel_worker_failed)
        thread_yield();

    if (kernel_worker_failed)
        console_write("testbin_capnames worker failed\n");
    (void)ipc_destroy(endpoint);
    (void)user_as;
    startup_reaper();
}
