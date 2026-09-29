#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

// where we map the source VMO in our own space, 64 TiB - this should accomodate any realistic ELF we could ever encounter in practice
#define TARGET_BASE 0x0000400000000000 

// TODO - at some point we really need to move a lot of this stuff into libsharkix
//        perhaps a shared ABI header for all the caps and other stuff?
#define VMO_READ  0x2
#define VMO_WRITE 0x4
#define VMO_EXEC  0x8

int map_vmo_self(uint64_t vmo, uintptr_t vaddr, uint64_t offset, uint64_t len, uint64_t map_rights) {
	sharkix_syscall_regs_t regs = { 0 };

	regs.rax = (uint64_t)SYSCALL_VM_MAP;
	regs.rdi = (uint64_t)vmo;
	regs.rsi = (uint64_t)vaddr;
	regs.rdx = (uint64_t)offset;
	regs.r10 = (uint64_t)len;
	regs.r8  = (uint64_t)map_rights;
	regs.r9  = (uint64_t)0;

	(void)sharkix_syscall(&regs);
	return regs.rax;
}

/* TODO: Replace this function with the userspace ELF loader. */
int elf_load(uint64_t source_vmo, uint64_t target_as, uint64_t *entry_out) {
	// first, let's map the source VMO into our space
	if(map_vmo_self(source_vmo,TARGET_BASE,0

	(void)source_vmo;
	(void)target_as;
	(void)entry_out;
	return -1;
}

static void send_status(uint64_t status_ipc, uint64_t status, uint64_t entry) {
	sharkix_syscall_regs_t regs = { 0 };

	regs.rax = (uint64_t)SYSCALL_IPC_SEND;
	regs.rdi = status_ipc;
	regs.rsi = status;
	regs.rdx = entry;
	(void)sharkix_syscall(&regs);
}

static void test_exit(void) {
	sharkix_syscall_regs_t regs = { 0 };

	regs.rax = SYSCALL_TEST_EXIT;
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
