#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>
#include <sharkix/elf64.h>

// where we map the source VMO in our own space, 64 TiB - this should accomodate any realistic ELF we could ever encounter in practice
#define ELF_LOAD_BASE 0x0000400000000000 

// TODO - at some point we really need to move a lot of this stuff into libsharkix
//        perhaps a shared ABI header for all the caps and other stuff?
#define VMO_READ  0x2
#define VMO_WRITE 0x4
#define VMO_EXEC  0x8

#define PAGE_SIZE 4096

int get_vmo_pagelen(uint64_t vmo, uint64_t *outlen) {
	sharkix_syscall_regs_t regs = { 0 };
	regs.rax = (uint64_t)SYSCALL_VM_GETLEN;
	regs.rdi = (uint64_t)vmo;
	(void)sharkix_syscall(&regs);
	if(regs.rax == 0) {
		*outlen = regs.rdx; // SUCCESS - correct page-rounded length of the VMO is here
		return 0;
	} else {
		*outlen = 0; // FAIL
		return -1; // TODO - again, i need to sort out the fucking errno situation
	}
}

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

int map_vmo_target(uint64_t as, uint64_t vmo, uint64_t vaddr, uint64_t offs, uint64_t len, uint64_t map_rights) {
	sharkix_syscall_regs_t regs = { 0 };

	regs.rax = (uint64_t)SYSCALL_AS_MAP;
	regs.rdi = as;
	regs.rsi = vmo;
	regs.rdx = vaddr;
	regs.r10 = offs;
	regs.r8  = len;
	regs.r9  = map_rights;

	(void)sharkix_syscall(&regs);
	return regs.rax;
}

int elf_load(uint64_t source_vmo, uint64_t target_as, uint64_t elf_file_len, uint64_t *entry_out) {
	// sanity checks first, get length etc
	uint64_t page_len=0;
	if(get_vmo_pagelen(source_vmo,&page_len) != 0) {
		sharkix_debug_puts("\nERROR! Could not get_vmo_pagelen()!\n");
		return -1;
	}
	if(page_len==0) {
		sharkix_debug_puts("\nERROR! The ELF is 0 bytes long???\n");
		return -1;
	}

	if(page_len < elf_file_len) {
		sharkix_debug_puts("\nERROR! Somehow the ELF's page-rounded length is shorter than the logical length???\n");
		return -1;
	}

	// now we can map the VMO, yay
	if(map_vmo_self(source_vmo,ELF_LOAD_BASE,0,page_len,VMO_READ) != 0) {
		sharkix_debug_puts("\nERROR! Could not map_vmo_self()!\n");
		return -1;
	}

	// and now we're able to do the serious work here
	sharkix_debug_puts("\nParsing ELF....\n");

	uint8_t *elf    = (void *)ELF_LOAD_BASE;
	size_t elf_len  = elf_file_len;

	if(elf_len < sizeof(Elf64_Ehdr)) {
		sharkix_debug_puts("\nERROR! The ELF is too small for even the header!\n");
		return -1;
	}

	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)elf;

	// check if there's a dead boyfriend involved here
	if((elf[0] != 0x7F) || (elf[1] != 'E') || (elf[2] != 'L') || (elf[3] != 'F')) {
		sharkix_debug_puts("\n");
		sharkix_debug_puts("ERROR! Owens sisters apparently created this file - the magic is wrong\n");
		sharkix_debug_puts("       Please rebuild this file before Jimmy Angelov ends up as a vengeful spirit\n");
		sharkix_debug_puts("       If you don't get the reference, you suck and have no culture\n"); // seriously, you suck
		return -1;
	}


	// ensure other fields are sane
	if(ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
		sharkix_debug_puts("\nERROR! ELF is not 64-bit\n");
		return -1;
	}

	if(ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
		sharkix_debug_puts("\nERROR! ELF is not little-endian\n");
		return -1;
	}

	if(ehdr->e_ident[EI_VERSION] != EV_CURRENT ||
	   ehdr->e_version != EV_CURRENT) {
		sharkix_debug_puts("\nERROR! Unsupported ELF version\n");
		return -1;
	}

	if(ehdr->e_machine != EM_X86_64) {
		sharkix_debug_puts("\nERROR! ELF is not for x86-64\n");
		return -1;
	}

	if(ehdr->e_type != ET_EXEC) {
		sharkix_debug_puts("\nERROR! ELF is not an executable\n");
		return -1;
	}

	if(ehdr->e_ehsize != sizeof(Elf64_Ehdr)) {
		sharkix_debug_puts("\nERROR! Unexpected ELF header size\n");
		return -1;
	}

	if(ehdr->e_phentsize != sizeof(Elf64_Phdr)) {
		sharkix_debug_puts("\nERROR! Unexpected ELF program header size\n");
		return -1;
	}

	// Make sure the program header table starts inside the file
	if(ehdr->e_phoff > elf_len) {
		sharkix_debug_puts("\nERROR! Program header table is outside the ELF\n");
		return -1;
	}

	/*
	 * Check that the entire program header table fits without doing an
	 * overflow-prone e_phoff + e_phnum * e_phentsize calculation.
	 */
	if(ehdr->e_phnum >
	   (elf_len - ehdr->e_phoff) / sizeof(Elf64_Phdr)) {
		sharkix_debug_puts("\nERROR! Program header table extends past EOF\n");
		return -1;
	}

	Elf64_Phdr *phdrs = (Elf64_Phdr *)(elf + ehdr->e_phoff);

	for(uint16_t i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr *phdr = &phdrs[i];

		if(phdr->p_type != PT_LOAD)
			continue;

		// file data must fit inside the ELF
		if(phdr->p_offset > elf_len ||
		   phdr->p_filesz > elf_len - phdr->p_offset) {
			sharkix_debug_puts("\nERROR! ELF segment extends past EOF\n");
			return -1;
		}

		// the in-memory segment cannot be smaller than its file contents
		if(phdr->p_filesz > phdr->p_memsz) {
			sharkix_debug_puts("\nERROR! ELF segment has filesz > memsz\n");
			return -1;
		}

		// an empty segment gives us nothing to map
		if(phdr->p_memsz == 0)
			continue;

		/*
		 * p_vaddr doesn't have to begin on a page boundary.
		 *
		 * If, for example:
		 *
		 *     p_vaddr = 0x401234
		 *
		 * then we actually map from 0x401000 and put the first byte
		 * of the segment 0x234 bytes into the backing VMO.
		 */
		uint64_t page_vaddr = phdr->p_vaddr & ~(PAGE_SIZE - 1);
		uint64_t page_offset = phdr->p_vaddr - page_vaddr;

		// make sure page_offset + p_memsz cannot wrap
		if(phdr->p_memsz > UINT64_MAX - page_offset) {
			sharkix_debug_puts("\nERROR! ELF segment size overflows address space\n");
			return -1;
		}

		uint64_t segment_size = page_offset + phdr->p_memsz;

		// round up to whole pages, checking that the rounding cannot wrap
		if(segment_size > UINT64_MAX - (PAGE_SIZE - 1)) {
			sharkix_debug_puts("\nERROR! ELF segment size overflows during page alignment\n");
			return -1;
		}

		uint64_t vmo_size =
		    (segment_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

		// also make sure the virtual address range itself doesn't wrap
		if(vmo_size > UINT64_MAX - page_vaddr) {
			sharkix_debug_puts("\nERROR! ELF segment virtual address range overflows\n");
			return -1;
		}

		/*
		 * Convert ELF permissions into sharkix VMO flags
		 */
		uint64_t prot = 0;

		if(phdr->p_flags & PF_R)
			prot |= VMO_READ;

		if(phdr->p_flags & PF_W)
			prot |= VMO_WRITE;

		if(phdr->p_flags & PF_X)
			prot |= VMO_EXEC;

		// calculate where to map the thing

		uint64_t file_page = phdr->p_offset & ~(PAGE_SIZE - 1);
		page_vaddr = phdr->p_vaddr & ~(PAGE_SIZE - 1);

		page_offset = phdr->p_vaddr - page_vaddr;

		uint64_t file_map_size = page_offset + phdr->p_filesz;

		file_map_size = (file_map_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	
		if(map_vmo_target(target_as,source_vmo,page_vaddr,file_page,file_map_size,prot) != 0) {
			sharkix_debug_puts("\nERROR! Failed map_vmo_target()!\n");
			return -1;
		}	


	}

	sharkix_debug_puts("\nELF LOADED! Returning to kernel\n");
	/*
	 * Loading succeeded. The ELF entry point is a virtual address in
	 * target_as; the loader doesn't jump there itself.
	 */
	*entry_out = ehdr->e_entry;

	return 0;

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

void testelf_loader_main(uint64_t *bootstrap, uint64_t elf_file_len) {
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

	status = elf_load(source_vmo_cap, target_as_cap, elf_file_len, &entry);
	if(status == 0) {
		send_status(ipc_status_cap,0,entry); // SUCCESS!
	} else {
		send_status(ipc_status_cap,1,0);     // FAIL :(
	}
	test_exit();
}
