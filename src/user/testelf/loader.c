#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>
#include <sharkix/elf64.h>

// where we map the source VMO in our own space, 64 TiB - this should accomodate any realistic ELF we could ever encounter in practice
#define ELF_LOAD_BASE 0x0000400000000000

/*
 * Temporary mappings used while copying ELF data live immediately below the
 * source image.  They grow downwards from 64 TiB and may use up to 16 TiB.
 *
 * Keeping this as an explicit range makes the layout easy to reason about:
 * scratch mappings cannot overlap the source image above them, and malformed
 * input cannot make them grow all the way down into the loader's ordinary
 * code, data, or stack mappings.
 */
#define ELF_SCRATCH_TOP    ELF_LOAD_BASE
#define ELF_SCRATCH_BOTTOM 0x0000300000000000

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

int create_anon(uint64_t vmo_factory, uint64_t req_size, uint64_t map_rights, uint64_t *actual_size, uint64_t *new_vmo) {
	sharkix_syscall_regs_t regs = { 0 };

	regs.rax = (uint64_t)SYSCALL_VM_CREATE_ANON;
	regs.rdi = vmo_factory;
	regs.rsi = req_size;
	regs.rdx = map_rights;

	(void)sharkix_syscall(&regs);
	if(regs.rax == 0) {
		*new_vmo     = regs.rsi;
		*actual_size = regs.rdx;
		return 0;
	} else {
		*actual_size = 0;
		return -1;
	}
}

/*
 * Reserve one page-aligned range in the loader's scratch area.
 *
 * Scratch mappings are never reused during one load.  The loader is a short
 * lived process, so keeping each temporary mapping until the loader exits is
 * simpler than teaching this test loader to unmap and recycle them.
 */
static int reserve_scratch_range(uint64_t size,
				 uint64_t *scratch_used,
				 uintptr_t *out_vaddr) {
	uint64_t scratch_capacity = ELF_SCRATCH_TOP - ELF_SCRATCH_BOTTOM;

	if(!scratch_used || !out_vaddr || size == 0 ||
	   (size & (PAGE_SIZE - 1)) != 0)
		return -1;

	if(*scratch_used > scratch_capacity ||
	   size > scratch_capacity - *scratch_used)
		return -1;

	*scratch_used += size;
	*out_vaddr = (uintptr_t)(ELF_SCRATCH_TOP - *scratch_used);
	return 0;
}

int elf_load(uint64_t source_vmo, uint64_t target_as, uint64_t elf_file_len, uint64_t vmo_factory, uint64_t *entry_out) {
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
	uint64_t scratch_used = 0;

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

		/*
		 * Writable segments must never be mapped directly from source_vmo.
		 *
		 * source_vmo contains the original ELF file and may be shared.  If a
		 * process could write through a mapping backed by that VMO, it would
		 * modify the original image and could affect another process loaded
		 * from the same source.
		 *
		 * Give every writable PT_LOAD one private anonymous VMO instead:
		 *
		 *   1. create zero-filled backing for the whole rounded segment;
		 *   2. map it temporarily into this loader;
		 *   3. copy only the p_filesz bytes supplied by the ELF;
		 *   4. map the private VMO into the target with the ELF permissions.
		 *
		 * The anonymous VMO starts zero-filled.  Bytes before an unaligned
		 * p_vaddr, the BSS from p_filesz to p_memsz, and final page padding
		 * therefore remain zero without needing separate special cases.
		 */
		if(phdr->p_flags & PF_W) {
			uint64_t private_vmo;
			uint64_t private_actual_size;
			uintptr_t scratch_vaddr;

			/*
			 * The loader needs WRITE while filling the VMO and READ because
			 * Sharkix cannot represent a present, non-readable x86 mapping.
			 * EXEC is retained only when the ELF requested it.
			 */
			uint64_t private_rights = prot | VMO_READ | VMO_WRITE;

			if(create_anon(vmo_factory,vmo_size,private_rights,
				       &private_actual_size,&private_vmo) != 0 ||
			   private_actual_size < vmo_size) {
				sharkix_debug_puts("\nERROR! Failed to create private VMO for writable ELF segment!\n");
				return -1;
			}

			if(reserve_scratch_range(vmo_size,&scratch_used,
						 &scratch_vaddr) != 0) {
				sharkix_debug_puts("\nERROR! Writable ELF segment does not fit in loader scratch space!\n");
				return -1;
			}

			if(map_vmo_self(private_vmo,scratch_vaddr,0,vmo_size,
					VMO_READ|VMO_WRITE) != 0) {
				sharkix_debug_puts("\nERROR! Failed to map private writable ELF segment into loader!\n");
				return -1;
			}

			/*
			 * p_filesz was bounds checked against the source ELF above, and
			 * page_offset + p_memsz was checked before vmo_size was rounded.
			 * Since p_filesz <= p_memsz, both ends of this copy are valid.
			 */
			uint8_t *src = elf + phdr->p_offset;
			uint8_t *dst = (uint8_t *)scratch_vaddr + page_offset;

			for(uint64_t j = 0; j < phdr->p_filesz; j++)
				dst[j] = src[j];

			if(map_vmo_target(target_as,private_vmo,page_vaddr,0,
					  vmo_size,prot) != 0) {
				sharkix_debug_puts("\nERROR! Failed to map private writable ELF segment into target!\n");
				return -1;
			}

			/* This segment is complete; the shared-source path is read-only. */
			continue;
		}

		// calculate where to map the thing

		uint64_t file_page = phdr->p_offset & ~(PAGE_SIZE - 1);
		page_vaddr = phdr->p_vaddr & ~(PAGE_SIZE - 1);

		page_offset = phdr->p_vaddr - page_vaddr;

		uint64_t file_size = page_offset + phdr->p_filesz;
		uint64_t direct_size = file_size & ~(PAGE_SIZE - 1);
		uint64_t mixed_size = file_size & (PAGE_SIZE - 1);

		/*
		 * If there is no BSS, the final partial page can stay directly
		 * backed by the source ELF VMO just like it always was.
		 */
		if(phdr->p_filesz == phdr->p_memsz) {
			direct_size = (file_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
			mixed_size = 0;
		}

		if(direct_size != 0) {
			if(map_vmo_target(target_as,source_vmo,page_vaddr,file_page,direct_size,prot) != 0) {
				sharkix_debug_puts("\nERROR! Failed map_vmo_target()!\n");
				return -1;
			}
		}

		uint64_t mapped_size = direct_size;

		/*
		 * If file data ends partway through a page and BSS continues past
		 * it, that page cannot remain backed by the ELF VMO: the bytes after
		 * p_filesz must be zero.  Make a private zero page and copy only the
		 * file-backed part into it.
		 */
		if(phdr->p_filesz < phdr->p_memsz && mixed_size != 0) {
			uint64_t mixed_vmo;
			uint64_t mixed_actual_size;

			if(create_anon(vmo_factory,PAGE_SIZE,prot | VMO_WRITE,&mixed_actual_size,&mixed_vmo) != 0 ||
			   mixed_actual_size < PAGE_SIZE) {
				sharkix_debug_puts("\nERROR! Failed to create anonymous VMO for mixed BSS page!\n");
				return -1;
			}

			uintptr_t scratch_vaddr;

			if(reserve_scratch_range(PAGE_SIZE,&scratch_used,
						 &scratch_vaddr) != 0) {
				sharkix_debug_puts("\nERROR! Mixed BSS page does not fit in loader scratch space!\n");
				return -1;
			}

			if(map_vmo_self(mixed_vmo,scratch_vaddr,0,PAGE_SIZE,VMO_READ|VMO_WRITE) != 0) {
				sharkix_debug_puts("\nERROR! Failed to map mixed BSS page into loader!\n");
				return -1;
			}

			uint64_t source_offset = file_page + direct_size;
			uint8_t *src = elf + source_offset;
			uint8_t *dst = (uint8_t *)scratch_vaddr;

			for(uint64_t j = 0; j < mixed_size; j++)
				dst[j] = src[j];

			if(map_vmo_target(target_as,mixed_vmo,page_vaddr + direct_size,0,PAGE_SIZE,prot) != 0) {
				sharkix_debug_puts("\nERROR! Failed to map mixed BSS page into target!\n");
				return -1;
			}

			mapped_size += PAGE_SIZE;
		}

		/*
		 * Anything left in the in-memory segment is pure BSS.  Anonymous
		 * VMOs are already zero-filled, so this can go straight into the
		 * target address space.
		 */
		if(mapped_size < vmo_size) {
			uint64_t bss_size = vmo_size - mapped_size;
			uint64_t bss_vmo;
			uint64_t bss_actual_size;

			if(create_anon(vmo_factory,bss_size,prot,&bss_actual_size,&bss_vmo) != 0 ||
			   bss_actual_size < bss_size) {
				sharkix_debug_puts("\nERROR! Failed to create anonymous VMO for BSS!\n");
				return -1;
			}

			if(map_vmo_target(target_as,bss_vmo,page_vaddr + mapped_size,0,bss_size,prot) != 0) {
				sharkix_debug_puts("\nERROR! Failed to map BSS into target!\n");
				return -1;
			}
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
		"elf.ipc.status",
		"factory.vmo"
	};
	uint64_t caps[4];
	uint64_t entry = 0;
	int status;

	if (sharkix_get_bootstrap(caps, names, 4, bootstrap) !=
		SHARKIX_BOOTSTRAP_OK)
		test_exit();

	uint64_t source_vmo_cap  = caps[0];
	uint64_t target_as_cap   = caps[1];
	uint64_t ipc_status_cap  = caps[2];
	uint64_t vmo_factory_cap = caps[3];

	status = elf_load(source_vmo_cap, target_as_cap, elf_file_len, vmo_factory_cap, &entry);
	if(status == 0) {
		send_status(ipc_status_cap,0,entry); // SUCCESS!
	} else {
		send_status(ipc_status_cap,1,0);     // FAIL :(
	}
	test_exit();
}
