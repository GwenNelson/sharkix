#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "console.h"
#include "ipc.h"
#include "memory.h"
#include "program.h"
#include "startup.h"
#include "thread.h"


extern const uint8_t ipc_consumer_image_start[];
extern const uint8_t ipc_consumer_image_end[];

extern const uint8_t ipc_producer_image_start[];
extern const uint8_t ipc_producer_image_end[];


/*
 * Create a user-mode task from a flat program image.
 *
 * The task is created but not started.  The top word of its user stack is
 * reserved for startup data; its kernel-accessible address is returned in
 * out_handle_slot.
 *
 * On success, ownership of the returned address space and thread is passed
 * to the caller.
 */
static int
create_user_task(const program_image_t *image,
                 const char *name,
                 address_space_t **out_address_space,
                 thread_t **out_thread,
                 uint64_t **out_handle_slot)
{
    address_space_t *address_space = NULL;
    thread_t *thread = NULL;
    uintptr_t stack_top;
    uint64_t physical;
    thread_create_params_t params;

    *out_address_space = NULL;
    *out_thread = NULL;
    *out_handle_slot = NULL;

    /*
     * Give each user task its own address space, then map its executable
     * image and initial user stack.
     */
    address_space = address_space_create(0);
    if (!address_space)
        goto failed;

    if (program_map_flat_image(address_space,
                               image,
                               PROGRAM_DEFAULT_LOAD_ADDRESS) != 0)
        goto failed;

    if (program_map_user_stack(address_space,
                               PROGRAM_DEFAULT_STACK_BASE,
                               PAGE_SIZE,
                               &stack_top) != 0)
        goto failed;

    /*
     * Reserve the top word of the stack for the IPC endpoint handle passed
     * to the test program.  Translate it now so the kernel can fill it in
     * after the endpoint has been created.
     */
    physical = address_space_translate(address_space,
                                       stack_top - sizeof(uint64_t));
    if (physical == UINT64_MAX)
        goto failed;

    params = (thread_create_params_t) {
        .entry_rip = PROGRAM_DEFAULT_LOAD_ADDRESS,
        .initial_stack_pointer = stack_top - sizeof(uint64_t),
        .name = name,
        .priority = tskIDLE_PRIORITY + 1
    };

    thread = thread_create(address_space, THREAD_PRIVILEGE_USER, &params);
    if (!thread)
        goto failed;

    *out_address_space = address_space;
    *out_thread = thread;
    *out_handle_slot = phys_to_virt(physical);

    return 0;

failed:
    if (thread)
        thread_destroy_unstarted(thread);

    if (address_space)
        address_space_release(address_space);

    return -1;
}


/*
 * Check the properties this startup profile relies upon:
 *
 *  - both tasks really are user-mode threads;
 *  - each task owns a distinct address space;
 *  - their CR3/PML4 roots are distinct;
 *  - mapping the same virtual program address in each task resolves to
 *    different physical memory.
 */
static int
validate_user_tasks(const address_space_t *consumer_as,
                    const address_space_t *producer_as,
                    const thread_t *consumer,
                    const thread_t *producer)
{
    uint64_t consumer_program_phys;
    uint64_t producer_program_phys;

    if (consumer->privilege != THREAD_PRIVILEGE_USER)
        return -1;

    if (producer->privilege != THREAD_PRIVILEGE_USER)
        return -1;

    if (consumer->address_space == producer->address_space)
        return -1;

    if (consumer_as->pml4_phys == producer_as->pml4_phys)
        return -1;

    consumer_program_phys =
        address_space_translate(consumer_as, PROGRAM_DEFAULT_LOAD_ADDRESS);

    producer_program_phys =
        address_space_translate(producer_as, PROGRAM_DEFAULT_LOAD_ADDRESS);

    if (consumer_program_phys == producer_program_phys)
        return -1;

    return 0;
}


/*
 * Startup profile exercising IPC between two isolated user-mode tasks.
 */
void kernel_startup_profile(void)
{
    const program_image_t consumer_image = {
        .data = ipc_consumer_image_start,
        .size = (size_t)(ipc_consumer_image_end - ipc_consumer_image_start)
    };

    const program_image_t producer_image = {
        .data = ipc_producer_image_start,
        .size = (size_t)(ipc_producer_image_end - ipc_producer_image_start)
    };

    address_space_t *consumer_as = NULL;
    address_space_t *producer_as = NULL;

    thread_t *consumer = NULL;
    thread_t *producer = NULL;

    uint64_t *consumer_handle_slot = NULL;
    uint64_t *producer_handle_slot = NULL;

    ipc_handle_t endpoint = IPC_INVALID_HANDLE;

    ipc_init();

    /*
     * Create the consumer first because it owns the endpoint used by this
     * test.
     */
    if (create_user_task(&consumer_image,
                         "ipc-consumer",
                         &consumer_as,
                         &consumer,
                         &consumer_handle_slot) != 0)
        goto failed;

    if (ipc_create(consumer, &endpoint) != IPC_OK)
        goto failed;

    /*
     * The producer gets its own address space, but receives a handle to the
     * consumer's endpoint.
     */
    if (create_user_task(&producer_image,
                         "ipc-producer",
                         &producer_as,
                         &producer,
                         &producer_handle_slot) != 0)
        goto failed;

    /*
     * Sanity-check that the test really is exercising IPC between isolated
     * user address spaces rather than accidentally sharing mappings.
     */
    if (validate_user_tasks(consumer_as, producer_as,
                            consumer, producer) != 0)
        goto failed;

    /*
     * Pass the endpoint handle to both programs through the word reserved
     * at the top of their initial user stacks.
     */
    *consumer_handle_slot = endpoint;
    *producer_handle_slot = endpoint;

    if (thread_start(consumer) != 0)
        goto failed;

    if (thread_start(producer) != 0)
        goto failed;

    console_write("user_ipc threads ");
    console_decimal(consumer->id);
    console_putc(' ');
    console_decimal(producer->id);

    console_write(" separate CR3 ");
    console_hex(consumer_as->pml4_phys);
    console_putc(' ');
    console_hex(producer_as->pml4_phys);
    console_putc('\n');

    startup_reaper();
    return;

failed:
    /*
     * Only unstarted (READY) threads can be destroyed here.  If startup
     * progressed further than that, normal thread teardown owns them.
     */
    if (endpoint != IPC_INVALID_HANDLE && consumer)
        (void)ipc_destroy(consumer, endpoint);

    if (consumer && consumer->state == THREAD_STATE_READY)
        thread_destroy_unstarted(consumer);

    if (producer && producer->state == THREAD_STATE_READY)
        thread_destroy_unstarted(producer);

    if (consumer_as)
        address_space_release(consumer_as);

    if (producer_as)
        address_space_release(producer_as);

    console_write("user_ipc setup failed\n");

    for (;;)
        __asm__ volatile ("cli; hlt");
}
