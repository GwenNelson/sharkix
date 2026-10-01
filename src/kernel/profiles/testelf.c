#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <sharkix/kernel/subsystems/kas.h>
#include <sharkix/kernel/subsystems/kcaps.h>
#include "console.h"
#include <sharkix/kernel/subsystems/kipc.h>
#include "memory.h"
#include <sharkix/kernel/subsystems/pmem.h>
#include "program.h"
#include "startup.h"
#include "thread.h"
#include <sharkix/kernel/subsystems/vmo.h>

enum {
    TESTELF_BOOTSTRAP_WORDS = 6,
    TESTELF_BOOTSTRAP_BYTES = TESTELF_BOOTSTRAP_WORDS * sizeof(uint64_t)
};

extern const uint8_t testelf_loader_image_start[];
extern const uint8_t testelf_loader_image_end[];
extern const uint8_t testelf_payload_image_start[];
extern const uint8_t testelf_payload_image_end[];

static int create_source_vmo(vmo_handle_t *out) {
	size_t image_size = (size_t)(testelf_payload_image_end -
								 testelf_payload_image_start);
	size_t page_count;
	size_t rounded_size;
	uint64_t physical;
	pmem_handle_t pmem;

	if (!image_size || image_size > SIZE_MAX - (PAGE_SIZE - 1))
		return -1;

	rounded_size = (image_size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
	page_count = rounded_size / PAGE_SIZE;
	if (!phys_alloc_pages(page_count, &physical))
		return -1;

	memset(phys_to_virt(physical), 0, rounded_size);
	memcpy(phys_to_virt(physical), testelf_payload_image_start, image_size);

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

	/* Keep the allocated pages as backing for the source VMO. */
	return 0;
}

static int create_loader_task(address_space_t **out_as, thread_t **out_thread,
                              uint64_t **out_bootstrap) {
	address_space_t *address_space = NULL;
	thread_t *thread = NULL;
	uintptr_t stack_top;
	uint64_t physical;
	thread_create_params_t params;
	const program_image_t image = {
		.data = testelf_loader_image_start,
		.size = (size_t)(testelf_loader_image_end - testelf_loader_image_start)
	};

	address_space = address_space_create(0);
	if (!address_space ||
		program_map_flat_image(address_space, &image,
							   PROGRAM_DEFAULT_LOAD_ADDRESS) != 0 ||
		program_map_user_stack(address_space, PROGRAM_DEFAULT_STACK_BASE,
							   PAGE_SIZE, &stack_top) != 0)
		goto failed;

	physical = address_space_translate(address_space,
									   stack_top - TESTELF_BOOTSTRAP_BYTES);
	if (physical == UINT64_MAX)
		goto failed;

	params = (thread_create_params_t) {
		.entry_rip = PROGRAM_DEFAULT_LOAD_ADDRESS,
		.initial_stack_pointer = stack_top - TESTELF_BOOTSTRAP_BYTES,
		.name = "testelf-loader", .priority = THREAD_PRIORITY_NORMAL
	};
	thread = thread_create(address_space, THREAD_PRIVILEGE_USER, &params);
	if (!thread)
		goto failed;

	*out_as = address_space;
	*out_thread = thread;
	*out_bootstrap = (uint64_t *)phys_to_virt(physical);
	return 0;

	failed:
	if (thread)
		thread_destroy_unstarted(thread);
	if (address_space)
		address_space_release(address_space);
	return -1;
}

static int install_named_cap(address_space_t *address_space,
                             kobject_handle_t object, cap_type_t type,
                             cap_rights_t rights, const char *name,
                             size_t name_length, cap_handle_t *out) {
	if (kcap_create(object, type, rights | CAP_RIGHT_GETNAME, out) != 0)
		return -1;
	if (kcap_set_name(*out, name, name_length) != 0 ||
		kcapset_addcap(address_space->capset, *out) != 0)
		return -1;
	return 0;
}

void kernel_startup_profile(void) {
	address_space_t *loader_as = NULL;
	address_space_t *target_as = NULL;
	thread_t *loader_thread = NULL;
	thread_t *target_thread;
	uint64_t *bootstrap = NULL;
	vmo_handle_t source_vmo;
	as_handle_t target_handle;
	ipc_handle_t status_endpoint;
	cap_handle_t source_cap;
	cap_handle_t target_cap;
	cap_handle_t status_cap;
	cap_handle_t vmo_factory_cap;
	ipc_message_t message;
	uintptr_t stack_top;
	thread_create_params_t params;

	for (unsigned i = 0; i < 5; ++i)
		thread_yield();
	console_write("\n\n\n");

	message = (ipc_message_t) { 0 };

	if (create_source_vmo(&source_vmo) != 0 ||
		!(target_as = address_space_create(0)) ||
		kas_register(&target_handle, target_as) != 0 ||
		kipc_create(&status_endpoint) != IPC_OK ||
		create_loader_task(&loader_as, &loader_thread, &bootstrap) != 0 ||
		install_named_cap(loader_as, source_vmo, CAP_TYPE_VMO,
						  CAP_RIGHT_VMO_MAP | CAP_RIGHT_VMO_READ |
						  CAP_RIGHT_VMO_EXEC |
						  CAP_RIGHT_VMO_GETLEN,
						  "elf.source.vmo", sizeof("elf.source.vmo") - 1,
						  &source_cap) != 0 ||
		install_named_cap(loader_as, target_handle, CAP_TYPE_AS,
						  CAP_RIGHT_AS_MAP | CAP_RIGHT_AS_PROTECT,
						  "elf.target.as", sizeof("elf.target.as") - 1,
						  &target_cap) != 0 ||
		install_named_cap(loader_as, status_endpoint, CAP_TYPE_IPC,
						  CAP_RIGHT_IPC_SEND,
						  "elf.ipc.status", sizeof("elf.ipc.status") - 1,
						  &status_cap) != 0 || 
		install_named_cap(loader_as, 0, CAP_TYPE_FACTORY_VMO,
						CAP_FACTORY_VMO_VALID_RIGHTS,
						"factory.vmo",sizeof("factory.vmo") -1,
						&vmo_factory_cap) != 0) {
		console_write("testelf startup failed\n");
		for (;;) __asm__ volatile ("cli; hlt");
	}

	bootstrap[0] = 4;
	bootstrap[1] = source_cap;
	bootstrap[2] = target_cap;
	bootstrap[3] = status_cap;
	bootstrap[4] = vmo_factory_cap;
	bootstrap[5] = (uint64_t)(testelf_payload_image_end -
	                          testelf_payload_image_start);

	if (thread_start(loader_thread) != 0) {
		console_write("testelf loader thread startup failed\n");
		for (;;) __asm__ volatile ("cli; hlt");
	}

	if (kipc_recv(status_endpoint, &message) != IPC_OK ||
		message.words[0] != 0) {
		console_write("testelf loader reported failure\n");
		(void)kas_unregister(target_handle);
		address_space_release(target_as);
		address_space_release(loader_as);
		startup_reaper();
		return;
	}

	if (message.words[1] == 0 || message.words[1] >= USER_CANONICAL_TOP ||
		address_space_translate(target_as, (uintptr_t)message.words[1]) == UINT64_MAX ||
		program_map_user_stack(target_as, PROGRAM_DEFAULT_STACK_BASE,
							   PAGE_SIZE, &stack_top) != 0) {
		console_write("testelf target setup failed\n");
		for (;;) __asm__ volatile ("cli; hlt");
	}

	params = (thread_create_params_t) {
		.entry_rip = (uintptr_t)message.words[1],
		.initial_stack_pointer = stack_top,
		.name = "testelf-target", .priority = THREAD_PRIORITY_NORMAL
	};
	target_thread = thread_create(target_as, THREAD_PRIVILEGE_USER, &params);
	if (!target_thread || thread_start(target_thread) != 0) {
		console_write("testelf target thread startup failed\n");
		for (;;) __asm__ volatile ("cli; hlt");
	}

	(void)kas_unregister(target_handle);
	address_space_release(target_as);
	address_space_release(loader_as);
	startup_reaper();
}
