#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <sharkix/kernel/subsystems/kcaps.h>
#include <sharkix/kernel/subsystems/kpmem.h>
#include <sharkix/kernel/subsystems/kvmo.h>
#include "console.h"
#include "memory.h"
#include "program.h"
#include "startup.h"
#include "thread.h"

enum {
    KTHREAD_TEST_BOOTSTRAP_WORDS = 6,
    KTHREAD_TEST_BOOTSTRAP_BYTES = KTHREAD_TEST_BOOTSTRAP_WORDS * sizeof(uint64_t)
};

extern const uint8_t kthread_test_image_start[];
extern const uint8_t kthread_test_image_end[];
extern const uint8_t taskA_image_start[];
extern const uint8_t taskA_image_end[];
extern const uint8_t taskB_image_start[];
extern const uint8_t taskB_image_end[];

static int create_payload_vmo(const uint8_t *image, size_t image_size,
                              vmo_handle_t *out)
{
    size_t rounded_size;
    size_t page_count;
    uint64_t physical;
    pmem_handle_t pmem;

    if (!image_size || image_size > SIZE_MAX - (PAGE_SIZE - 1))
        return -1;

    rounded_size = (image_size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    page_count = rounded_size / PAGE_SIZE;
    if (!phys_alloc_pages(page_count, &physical))
        return -1;

    memset(phys_to_virt(physical), 0, rounded_size);
    memcpy(phys_to_virt(physical), image, image_size);
    if (kpmem_create(&pmem, physical, rounded_size) != 0) {
        for (size_t i = 0; i < page_count; ++i)
            phys_page_put(physical + i * PAGE_SIZE);
        return -1;
    }

    if (kvmo_create_from_pmem(out, pmem, VMO_MAP | VMO_READ | VMO_EXEC) != 0) {
        (void)kpmem_destroy(pmem);
        for (size_t i = 0; i < page_count; ++i)
            phys_page_put(physical + i * PAGE_SIZE);
        return -1;
    }
    return 0;
}

static int create_user_task(address_space_t **out_as, thread_t **out_thread,
                            uint64_t **out_bootstrap)
{
    address_space_t *address_space;
    uintptr_t stack_top;
    uint64_t physical;
    thread_create_params_t params;
    const program_image_t image = {
        .data = kthread_test_image_start,
        .size = (size_t)(kthread_test_image_end - kthread_test_image_start)
    };

    address_space = address_space_create(0);
    if (!address_space ||
        program_map_flat_image(address_space, &image,
                               PROGRAM_DEFAULT_LOAD_ADDRESS) != 0 ||
        program_map_user_stack(address_space, PROGRAM_DEFAULT_STACK_BASE,
                               PAGE_SIZE, &stack_top) != 0)
        return -1;

    physical = address_space_translate(address_space,
                                       stack_top - KTHREAD_TEST_BOOTSTRAP_BYTES);
    if (physical == UINT64_MAX)
        return -1;

    params = (thread_create_params_t) {
        .entry_rip = PROGRAM_DEFAULT_LOAD_ADDRESS,
        .initial_stack_pointer = stack_top - KTHREAD_TEST_BOOTSTRAP_BYTES,
        .name = "kthread-test", .priority = THREAD_PRIORITY_NORMAL
    };
    *out_thread = thread_create(address_space, THREAD_PRIVILEGE_USER, &params);
    if (!*out_thread)
        return -1;

    *out_as = address_space;
    *out_bootstrap = (uint64_t *)phys_to_virt(physical);
    return 0;
}

static int install_named_cap(address_space_t *address_space,
                             kobject_handle_t object, cap_type_t type,
                             cap_rights_t rights, const char *name,
                             size_t name_length, cap_handle_t *out)
{
    if (kcap_create(object, type, rights | CAP_RIGHT_GETNAME, out) != 0)
        return -1;
    if (kcap_set_name(*out, name, name_length) != 0 ||
        kcapset_addcap(address_space->capset, *out) != 0)
        return -1;
    return 0;
}

void kernel_startup_profile(void)
{
    address_space_t *user_as;
    thread_t *user_thread;
    uint64_t *bootstrap;
    vmo_handle_t payload_a;
    vmo_handle_t payload_b;
    cap_handle_t as_factory;
    cap_handle_t thread_factory;
    cap_handle_t vmo_factory;
    cap_handle_t payload_a_cap;
    cap_handle_t payload_b_cap;
    const cap_rights_t payload_rights = CAP_RIGHT_VMO_MAP |
                                         CAP_RIGHT_VMO_READ |
                                         CAP_RIGHT_VMO_EXEC |
                                         CAP_RIGHT_VMO_GETLEN;

    if (create_payload_vmo(taskA_image_start,
                           (size_t)(taskA_image_end - taskA_image_start),
                           &payload_a) != 0 ||
        create_payload_vmo(taskB_image_start,
                           (size_t)(taskB_image_end - taskB_image_start),
                           &payload_b) != 0 ||
        create_user_task(&user_as, &user_thread, &bootstrap) != 0 ||
        install_named_cap(user_as, 0, CAP_TYPE_FACTORY_AS, 0,
                          "factory.as", sizeof("factory.as") - 1,
                          &as_factory) != 0 ||
        install_named_cap(user_as, 0, CAP_TYPE_FACTORY_THREAD, 0,
                          "factory.thread", sizeof("factory.thread") - 1,
                          &thread_factory) != 0 ||
        install_named_cap(user_as, 0, CAP_TYPE_FACTORY_VMO, 0,
                          "factory.vmo", sizeof("factory.vmo") - 1,
                          &vmo_factory) != 0 ||
        install_named_cap(user_as, payload_a, CAP_TYPE_VMO, payload_rights,
                          "vmo.payload.a", sizeof("vmo.payload.a") - 1,
                          &payload_a_cap) != 0 ||
        install_named_cap(user_as, payload_b, CAP_TYPE_VMO, payload_rights,
                          "vmo.payload.b", sizeof("vmo.payload.b") - 1,
                          &payload_b_cap) != 0) {
        console_write("kthread_test startup failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    bootstrap[0] = 5;
    bootstrap[1] = as_factory;
    bootstrap[2] = thread_factory;
    bootstrap[3] = vmo_factory;
    bootstrap[4] = payload_a_cap;
    bootstrap[5] = payload_b_cap;

    if (thread_start(user_thread) != 0) {
        console_write("kthread_test thread startup failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    (void)user_as;
    startup_reaper();
}
