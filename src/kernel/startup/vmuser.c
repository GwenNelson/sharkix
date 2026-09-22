#include <stddef.h>
#include <stdint.h>

#include "caps.h"
#include "console.h"
#include "ipc.h"
#include "memory.h"
#include "pmem.h"
#include "program.h"
#include "startup.h"
#include "thread.h"
#include "vmo.h"

enum {
    VMUSER_BOOTSTRAP_WORDS = 3,
    VMUSER_MAP_VA = 0x600000
};

extern const uint8_t vmuser_a_image_start[];
extern const uint8_t vmuser_a_image_end[];
extern const uint8_t vmuser_b_image_start[];
extern const uint8_t vmuser_b_image_end[];

static int create_user_task(const program_image_t *image,
                            const char *name,
                            address_space_t **out_as,
                            thread_t **out_thread,
                            uint64_t **out_bootstrap)
{
    address_space_t *as;
    uintptr_t stack_top;
    uint64_t physical;
    thread_create_params_t params;

    as = address_space_create(0);
    if (!as ||
        program_map_flat_image(as, image, PROGRAM_DEFAULT_LOAD_ADDRESS) != 0 ||
        program_map_user_stack(as, PROGRAM_DEFAULT_STACK_BASE, PAGE_SIZE,
                               &stack_top) != 0)
        return -1;

    physical = address_space_translate(as,
                                       stack_top - VMUSER_BOOTSTRAP_WORDS *
                                       sizeof(uint64_t));
    if (physical == UINT64_MAX)
        return -1;

    params = (thread_create_params_t) {
        .entry_rip = PROGRAM_DEFAULT_LOAD_ADDRESS,
        .initial_stack_pointer = stack_top - VMUSER_BOOTSTRAP_WORDS *
                                 sizeof(uint64_t),
        .name = name,
        .priority = THREAD_PRIORITY_NORMAL
    };

    *out_thread = thread_create(as, THREAD_PRIVILEGE_USER, &params);
    if (!*out_thread)
        return -1;

    *out_as = as;
    *out_bootstrap = (uint64_t *)phys_to_virt(physical);
    return 0;
}

static int install_cap(address_space_t *as, cap_handle_t cap)
{
    return kcapset_addcap(as->capset, cap);
}

static int wait_for_completion(ipc_handle_t endpoint, uint64_t expected)
{
    ipc_message_t message = { 0 };

    if (ipc_recv(endpoint, &message) != IPC_OK)
        return -1;

    return message.type == IPC_MSGTYPE_SEND && message.words[0] == expected
           ? 0 : -1;
}

static int send_continue(ipc_handle_t endpoint)
{
    ipc_message_t message = { 0 };

    message.type = IPC_MSGTYPE_SEND;
    message.words[0] = 1;
    return ipc_send_nb(thread_current(), endpoint, &message) == IPC_OK ? 0 : -1;
}

static void vmuser_fail(void)
{
    console_write("vmuser test failed\n");
    for (;;) thread_yield();
}

void kernel_startup_profile(void)
{
    const program_image_t image_a = {
        .data = vmuser_a_image_start,
        .size = (size_t)(vmuser_a_image_end - vmuser_a_image_start)
    };
    const program_image_t image_b = {
        .data = vmuser_b_image_start,
        .size = (size_t)(vmuser_b_image_end - vmuser_b_image_start)
    };
    address_space_t *as_a = NULL;
    address_space_t *as_b = NULL;
    thread_t *task_a = NULL;
    thread_t *task_b = NULL;
    uint64_t *bootstrap_a = NULL;
    uint64_t *bootstrap_b = NULL;
    uint64_t backing_page;
    uint64_t baseline_reaped;
    pmem_handle_t pmem;
    vmo_handle_t vmo;
    ipc_handle_t completion;
    ipc_handle_t control_a;
    ipc_handle_t control_b;
    cap_handle_t a_vmo_cap;
    cap_handle_t b_vmo_cap;
    cap_handle_t a_completion_cap;
    cap_handle_t b_completion_cap;
    cap_handle_t a_control_cap;
    cap_handle_t b_control_cap;

    thread_yield();
    thread_yield();
    thread_yield();
    thread_yield();
    thread_yield();
    console_write("\n\n\n\n\nVM userspace test:");

    startup_reaper();
    baseline_reaped = thread_reaped_count();

    if (!phys_alloc_page(&backing_page) ||
        kpmem_create(&pmem, (uintptr_t)backing_page, PAGE_SIZE) != 0 ||
        kvmo_create(&vmo, pmem, VMO_MAP | VMO_READ) != 0)
        vmuser_fail();
    console_write(" VMO created\n");

    if (kcap_create((kobject_handle_t)vmo, CAP_TYPE_VMO,
                    CAP_RIGHT_VMO_MAP | CAP_RIGHT_VMO_READ,
                    &a_vmo_cap) != 0 ||
        kcap_create((kobject_handle_t)vmo, CAP_TYPE_VMO, 0, &b_vmo_cap) != 0)
        vmuser_fail();
    console_write("VMO caps created\n");

    if (ipc_create(&completion) != IPC_OK ||
        ipc_create(&control_a) != IPC_OK ||
        ipc_create(&control_b) != IPC_OK ||
        kcap_create((kobject_handle_t)completion, CAP_TYPE_IPC_ENDPOINT,
                    CAP_RIGHT_IPC_SEND, &a_completion_cap) != 0 ||
        kcap_create((kobject_handle_t)completion, CAP_TYPE_IPC_ENDPOINT,
                    CAP_RIGHT_IPC_SEND, &b_completion_cap) != 0 ||
        kcap_create((kobject_handle_t)control_a, CAP_TYPE_IPC_ENDPOINT,
                    CAP_RIGHT_IPC_RECV, &a_control_cap) != 0 ||
        kcap_create((kobject_handle_t)control_b, CAP_TYPE_IPC_ENDPOINT,
                    CAP_RIGHT_IPC_RECV, &b_control_cap) != 0 ||
        create_user_task(&image_a, "vmuser-a", &as_a, &task_a,
                         &bootstrap_a) != 0 ||
        create_user_task(&image_b, "vmuser-b", &as_b, &task_b,
                         &bootstrap_b) != 0 ||
        install_cap(as_a, a_vmo_cap) != 0 ||
        install_cap(as_a, a_completion_cap) != 0 ||
        install_cap(as_a, a_control_cap) != 0 ||
        install_cap(as_b, b_vmo_cap) != 0 ||
        install_cap(as_b, b_completion_cap) != 0 ||
        install_cap(as_b, b_control_cap) != 0)
        vmuser_fail();

    bootstrap_a[0] = a_vmo_cap;
    bootstrap_a[1] = a_completion_cap;
    bootstrap_a[2] = a_control_cap;
    bootstrap_b[0] = b_vmo_cap;
    bootstrap_b[1] = b_completion_cap;
    bootstrap_b[2] = b_control_cap;

    if (thread_start(task_b) != 0 || thread_start(task_a) != 0)
        vmuser_fail();

    if (wait_for_completion(completion, 'A') != 0 ||
        send_continue(control_b) != 0 ||
        wait_for_completion(completion, 'B') != 0 ||
        send_continue(control_a) != 0 ||
        wait_for_completion(completion, 'U') != 0)
        vmuser_fail();

    console_write("\n\nthis should now page fault\n");
    if (send_continue(control_a) != 0)
        vmuser_fail();

    while (thread_reaped_count() < baseline_reaped + 2)
        thread_yield();

    (void)ipc_destroy(completion);
    (void)ipc_destroy(control_a);
    (void)ipc_destroy(control_b);
    (void)kcap_destroy(a_vmo_cap);
    (void)kcap_destroy(b_vmo_cap);
    (void)kcap_destroy(a_completion_cap);
    (void)kcap_destroy(b_completion_cap);
    (void)kcap_destroy(a_control_cap);
    (void)kcap_destroy(b_control_cap);
    (void)kvmo_destroy(vmo);
    (void)kpmem_destroy(pmem);
    phys_page_put(backing_page);

    console_write("\n\nvmuser verified\n");
    for (;;) thread_yield();
}
