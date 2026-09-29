#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

#define SYS_IPC_SEND (-202)
#define SYS_TEST_EXIT 1

/* TODO: Replace this function with the userspace ELF loader. */
int elf_load(uint64_t source_vmo, uint64_t target_as, uint64_t *entry_out) {
	(void)source_vmo;
	(void)target_as;
	(void)entry_out;
	return -1;
}

static void send_status(uint64_t status_ipc, uint64_t status, uint64_t entry) {
	sharkix_syscall_regs_t regs = { 0 };

	regs.rax = (uint64_t)SYS_IPC_SEND;
	regs.rdi = status_ipc;
	regs.rsi = status;
	regs.rdx = entry;
	(void)sharkix_syscall(&regs);
}

static void test_exit(void) {
	sharkix_syscall_regs_t regs = { 0 };

	regs.rax = SYS_TEST_EXIT;
	(void)sharkix_syscall(&regs);
	for (;;)
		__asm__ volatile ("pause");
}

void testelf_loader_main(uint64_t *bootstrap) {
	char *names[] = {
		"elf.source.vmo",
		"elf.target.as",
		"elf.ipc.status"
	};
	uint64_t caps[3];
	uint64_t entry = 0;
	int status;

	if (sharkix_get_bootstrap(caps, names, 3, bootstrap) !=
		SHARKIX_BOOTSTRAP_OK)
		test_exit();

	uint64_t source_vmo_cap = caps[0];
	uint64_t target_as_cap  = caps[1];
	uint64_t ipc_status_cap = caps[2];

	status = elf_load(source_vmo_cap, target_as_cap, &entry);
	if(status == 0) {
		send_status(ipc_status_cap,0,entry); // SUCCESS!
	} else {
		send_status(ipc_status_cap,1,0);     // FAIL :(
	}
	test_exit();
}
