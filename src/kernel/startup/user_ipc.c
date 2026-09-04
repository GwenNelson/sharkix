#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "ipc.h"
#include "memory.h"
#include "program.h"
#include "startup.h"
#include "thread.h"

extern const uint8_t ipc_consumer_image_start[], ipc_consumer_image_end[];
extern const uint8_t ipc_producer_image_start[], ipc_producer_image_end[];

static int create_user_task(const program_image_t *image, const char *name,
                            address_space_t **out_address_space,
                            thread_t **out_thread, uint64_t **out_handle_slot)
{
    address_space_t *address_space;
    uintptr_t stack_top;
    uint64_t physical;
    thread_create_params_t params;

    *out_address_space = NULL;
    *out_thread = NULL;
    *out_handle_slot = NULL;
    address_space = address_space_create(0);
    if (!address_space ||
        program_map_flat_image(address_space, image, PROGRAM_DEFAULT_LOAD_ADDRESS) != 0 ||
        program_map_user_stack(address_space, PROGRAM_DEFAULT_STACK_BASE, PAGE_SIZE, &stack_top) != 0)
        goto failed;

    physical = address_space_translate(address_space, stack_top - sizeof(uint64_t));
    if (physical == UINT64_MAX) goto failed;
    params = (thread_create_params_t){
        .entry_rip = PROGRAM_DEFAULT_LOAD_ADDRESS,
        .initial_stack_pointer = stack_top - sizeof(uint64_t),
        .name = name,
        .priority = tskIDLE_PRIORITY + 1
    };
    *out_thread = thread_create(address_space, THREAD_PRIVILEGE_USER, &params);
    if (!*out_thread) goto failed;
    *out_address_space = address_space;
    *out_handle_slot = phys_to_virt(physical);
    return 0;
failed:
    if (*out_thread) {
        thread_destroy_unstarted(*out_thread);
        *out_thread = NULL;
    }
    if (address_space) address_space_release(address_space);
    return -1;
}

void kernel_startup_profile(void)
{
    program_image_t consumer_image = {
        ipc_consumer_image_start,
        (size_t)(ipc_consumer_image_end - ipc_consumer_image_start)
    };
    program_image_t producer_image = {
        ipc_producer_image_start,
        (size_t)(ipc_producer_image_end - ipc_producer_image_start)
    };
    address_space_t *consumer_as = NULL;
    address_space_t *producer_as = NULL;
    thread_t *consumer = NULL;
    thread_t *producer = NULL;
    uint64_t *consumer_handle_slot = NULL;
    uint64_t *producer_handle_slot = NULL;
    ipc_handle_t endpoint = IPC_INVALID_HANDLE;

    ipc_init();
    if (create_user_task(&consumer_image, "ipc-consumer", &consumer_as, &consumer,
                         &consumer_handle_slot) != 0 ||
        ipc_create(consumer, &endpoint) != IPC_OK ||
        create_user_task(&producer_image, "ipc-producer", &producer_as, &producer,
                         &producer_handle_slot) != 0 ||
        consumer->privilege != THREAD_PRIVILEGE_USER ||
        producer->privilege != THREAD_PRIVILEGE_USER ||
        consumer->address_space == producer->address_space ||
        consumer_as->pml4_phys == producer_as->pml4_phys ||
        address_space_translate(consumer_as, PROGRAM_DEFAULT_LOAD_ADDRESS) ==
            address_space_translate(producer_as, PROGRAM_DEFAULT_LOAD_ADDRESS))
        goto failed;

    *consumer_handle_slot = endpoint;
    *producer_handle_slot = endpoint;
    if (thread_start(consumer) != 0 || thread_start(producer) != 0) goto failed;

    console_write("user_ipc threads ");
    console_decimal(consumer->id);
    console_putc(' ');
    console_decimal(producer->id);
    console_write(" separate CR3 ");
    console_hex(consumer_as->pml4_phys);
    console_putc(' ');
    console_hex(producer_as->pml4_phys);
    console_write("\n");
    startup_reaper();
    return;

failed:
    if (endpoint != IPC_INVALID_HANDLE && consumer)
        (void)ipc_destroy(consumer, endpoint);
    if (consumer && consumer->state == THREAD_STATE_READY) thread_destroy_unstarted(consumer);
    if (producer && producer->state == THREAD_STATE_READY) thread_destroy_unstarted(producer);
    if (consumer_as) address_space_release(consumer_as);
    if (producer_as) address_space_release(producer_as);
    console_write("user_ipc setup failed\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
