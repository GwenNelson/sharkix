#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "program.h"

int program_map_flat_image(address_space_t *address_space, const program_image_t *image,
                           uintptr_t load_address)
{
    if (!address_space || !image || !image->data || !image->size ||
        (load_address & (PAGE_SIZE - 1))) return -1;
    size_t mapped = 0;
    while (mapped < image->size) {
        uint64_t page;
        size_t count = image->size - mapped;
        if (count > PAGE_SIZE) count = PAGE_SIZE;
        if (!phys_alloc_page(&page)) {
            while (mapped) { mapped -= PAGE_SIZE; address_space_unmap_page(address_space, load_address + mapped); }
            return -1;
        }
        uint8_t *destination = (uint8_t *)phys_to_virt(page);
        for (size_t i = 0; i < count; ++i) destination[i] = image->data[mapped + i];
        if (address_space_map_page(address_space, load_address + mapped, page,
                                   PAGE_USER | ADDRESS_SPACE_MAP_OWNED) != 0) {
            phys_page_put(page);
            while (mapped) { mapped -= PAGE_SIZE; address_space_unmap_page(address_space, load_address + mapped); }
            return -1;
        }
        mapped += PAGE_SIZE;
    }
    return 0;
}

int program_map_user_stack(address_space_t *address_space, uintptr_t stack_base,
                           size_t stack_size, uintptr_t *stack_top)
{
    if (!address_space || !stack_size || (stack_base & (PAGE_SIZE - 1))) return -1;
    size_t rounded = (stack_size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    for (size_t offset = 0; offset < rounded; offset += PAGE_SIZE) {
        uint64_t page;
        if (!phys_alloc_page(&page)) {
            while (offset) { offset -= PAGE_SIZE; address_space_unmap_page(address_space, stack_base + offset); }
            return -1;
        }
        if (address_space_map_page(address_space, stack_base + offset, page,
                                   PAGE_USER | PAGE_WRITABLE | PAGE_NX | ADDRESS_SPACE_MAP_OWNED) != 0) {
            phys_page_put(page);
            while (offset) { offset -= PAGE_SIZE; address_space_unmap_page(address_space, stack_base + offset); }
            return -1;
        }
    }
    if (stack_top) *stack_top = stack_base + rounded;
    return 0;
}

int program_load_and_start(const program_image_t *image, const program_start_options_t *options,
                           address_space_t **out_address_space, thread_t **out_thread)
{
    if (out_address_space) *out_address_space = NULL;
    if (out_thread) *out_thread = NULL;
    if (!image || !options || options->privilege != THREAD_PRIVILEGE_USER) return -1;
    uintptr_t load = options->load_address ? options->load_address : PROGRAM_DEFAULT_LOAD_ADDRESS;
    uintptr_t entry = options->entry_address ? options->entry_address : load;
    uintptr_t stack_base = options->stack_base ? options->stack_base : PROGRAM_DEFAULT_STACK_BASE;
    size_t stack_size = options->stack_size ? options->stack_size : PAGE_SIZE;
    address_space_t *address_space = address_space_create(options->reap_on_exit ? ADDRESS_SPACE_REAP_WHEN_THREADLESS : 0);
    if (!address_space) return -1;
    if (program_map_flat_image(address_space, image, load) != 0) goto failed_as;
    uintptr_t stack_top;
    if (program_map_user_stack(address_space, stack_base, stack_size, &stack_top) != 0) goto failed_as;
    thread_create_params_t params = {
        .entry_rip = entry, .initial_stack_pointer = stack_top,
        .kernel_stack_size = options->kernel_stack_size, .name = options->name,
        .priority = options->priority, .argument = NULL
    };
    thread_t *thread = thread_create(address_space, THREAD_PRIVILEGE_USER, &params);
    if (!thread) goto failed_as;
    if (thread_start(thread) != 0) {
        thread_destroy_unstarted(thread);
        goto failed_as;
    }
    if (out_thread) *out_thread = thread;
    if (out_address_space) *out_address_space = address_space;
    else address_space_release(address_space);
    return 0;
failed_as:
    address_space_release(address_space);
    return -1;
}
