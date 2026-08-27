/*
 * bootstub32.c
 *
 * Minimal ELF64 PT_LOAD loader / Multiboot-1 trampoline.
 *
 * No libc, no heap, no paging, no long-mode transition.
 *
 * Required input:
 *
 *   module 0 : real ELF64 kernel, with a 32-bit physical entry point
 *   module 1 : rootserver / first module passed to the real kernel
 *   module 2+: arbitrary additional Multiboot modules
 *
 * The stub removes module 0 from the Multiboot module array in-place,
 * leaving every later module (including its command-line/name pointer)
 * unchanged.
 */

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;

#define MB_BOOTLOADER_MAGIC	0x2badb002u

#define MB_INFO_MODS		(1u << 3)

#define ELFCLASS64		2
#define ELFDATA2LSB		1
#define EM_X86_64		62
#define PT_LOAD			1

struct u64le {
	u32 lo;
	u32 hi;
} __attribute__((packed));

struct multiboot_module {
	u32 start;
	u32 end;
	u32 string;
	u32 reserved;
} __attribute__((packed));

/*
 * Multiboot-1 information structure.
 * Fields after mods_addr are included so the layout exactly matches the ABI;
 * bootstub32 only modifies mods_count and the module array itself.
 */
struct multiboot_info {
	u32 flags;
	u32 mem_lower;
	u32 mem_upper;
	u32 boot_device;
	u32 cmdline;
	u32 mods_count;
	u32 mods_addr;

	u32 syms[4];

	u32 mmap_length;
	u32 mmap_addr;
	u32 drives_length;
	u32 drives_addr;
	u32 config_table;
	u32 boot_loader_name;
	u32 apm_table;

	u32 vbe_control_info;
	u32 vbe_mode_info;
	u16 vbe_mode;
	u16 vbe_interface_seg;
	u16 vbe_interface_off;
	u16 vbe_interface_len;
} __attribute__((packed));

struct elf64_ehdr {
	u8  ident[16];
	u16 type;
	u16 machine;
	u32 version;
	struct u64le entry;
	struct u64le phoff;
	struct u64le shoff;
	u32 flags;
	u16 ehsize;
	u16 phentsize;
	u16 phnum;
	u16 shentsize;
	u16 shnum;
	u16 shstrndx;
} __attribute__((packed));

struct elf64_phdr {
	u32 type;
	u32 flags;
	struct u64le offset;
	struct u64le vaddr;
	struct u64le paddr;
	struct u64le filesz;
	struct u64le memsz;
	struct u64le align;
} __attribute__((packed));

extern u8 __stub_start[];
extern u8 __stub_end[];

/*
 * QEMU/Bochs debug console at I/O port 0xe9.
 * This has no dependencies on VGA, libc, memory allocation or interrupts.
 */

#ifdef BOOTSTUB_DEBUG_E9
static inline void debug_char(char c)
{
	__asm__ volatile ("outb %0, $0xe9" : : "a" ((unsigned char)c));
}
#else
static inline void debug_char(char c)
{
	(void)c;
}
#endif


static __attribute__((noreturn)) void fail(const char *message)
{
	volatile u16 *vga = (volatile u16 *)0xb8000;
	const char prefix[] = "bootstub32: ";

	unsigned int p = 0;

	for (unsigned int i = 0; prefix[i] && p < 80; i++)
		vga[p++] = (u16)prefix[i] | 0x4f00;

	for (unsigned int i = 0; message[i] && p < 80; i++)
		vga[p++] = (u16)message[i] | 0x4f00;

	for (;;) {
		__asm__ volatile ("cli; hlt");
	}
}

static int ranges_overlap(u32 a_start, u32 a_end, u32 b_start, u32 b_end)
{
	return a_start < b_end && b_start < a_end;
}

static void copy_bytes(u8 *dst, const u8 *src, u32 count)
{
	while (count--)
		*dst++ = *src++;
}

static void zero_bytes(u8 *dst, u32 count)
{
	while (count--)
		*dst++ = 0;
}

static u32 checked_low32(struct u64le value, const char *what)
{
	if (value.hi != 0)
		fail(what);

	return value.lo;
}

static u32 load_kernel_elf(struct multiboot_module *kernel_module)
{
	u32 image_start = kernel_module->start;
	u32 image_end   = kernel_module->end;

	if (image_end <= image_start)
		fail("kernel module has invalid size");

	u32 image_size = image_end - image_start;

	if (image_size < sizeof(struct elf64_ehdr))
		fail("kernel module is too small");

	struct elf64_ehdr *eh = (struct elf64_ehdr *)image_start;

	if (eh->ident[0] != 0x7f ||
	    eh->ident[1] != 'E' ||
	    eh->ident[2] != 'L' ||
	    eh->ident[3] != 'F')
		fail("module 0 is not ELF");

	if (eh->ident[4] != ELFCLASS64)
		fail("module 0 is not ELF64");

	if (eh->ident[5] != ELFDATA2LSB)
		fail("kernel ELF is not little-endian");

	if (eh->machine != EM_X86_64)
		fail("kernel ELF is not x86-64");

	if (eh->phentsize != sizeof(struct elf64_phdr))
		fail("unexpected ELF program-header size");

	u32 phoff = checked_low32(eh->phoff, "ELF phoff is above 4GiB");

	if (eh->phnum != 0) {
		u32 phbytes = (u32)eh->phnum * (u32)sizeof(struct elf64_phdr);

		if (phoff > image_size || phbytes > image_size - phoff)
			fail("ELF program headers outside module");
	}

	struct elf64_phdr *ph =
		(struct elf64_phdr *)(image_start + phoff);

	for (u32 i = 0; i < eh->phnum; i++) {
		if (ph[i].type != PT_LOAD)
			continue;

		u32 offset = checked_low32(ph[i].offset,
		    "ELF segment offset is above 4GiB");
		u32 paddr = checked_low32(ph[i].paddr,
		    "ELF segment paddr is above 4GiB");
		u32 filesz = checked_low32(ph[i].filesz,
		    "ELF segment filesz is above 4GiB");
		u32 memsz = checked_low32(ph[i].memsz,
		    "ELF segment memsz is above 4GiB");

		if (filesz > memsz)
			fail("ELF segment filesz > memsz");

		if (offset > image_size || filesz > image_size - offset)
			fail("ELF segment outside module");

		if (memsz > 0xffffffffu - paddr)
			fail("ELF segment address overflow");

		u32 dst_end = paddr + memsz;

		/*
		 * Keep the first implementation intentionally simple:
		 * reject self-overwrite and source-image overwrite rather
		 * than attempting clever overlapping copies.
		 */
		if (ranges_overlap(paddr, dst_end,
		    (u32)__stub_start, (u32)__stub_end))
			fail("kernel PT_LOAD overlaps bootstub32");

		if (ranges_overlap(paddr, dst_end, image_start, image_end))
			fail("kernel PT_LOAD overlaps kernel ELF source");

		copy_bytes((u8 *)paddr,
		    (const u8 *)(image_start + offset),
		    filesz);

		zero_bytes((u8 *)(paddr + filesz), memsz - filesz);
	}

	return checked_low32(eh->entry,
	    "kernel entry point is above 4GiB");
}


static void remove_kernel_module(struct multiboot_info *mbi)
{
	struct multiboot_module *mods =
		(struct multiboot_module *)mbi->mods_addr;

	for (u32 i = 1; i < mbi->mods_count; i++) {
		mods[i - 1].start    = mods[i].start;
		mods[i - 1].end      = mods[i].end;
		mods[i - 1].string   = mods[i].string;
		mods[i - 1].reserved = mods[i].reserved;
	}

	mbi->mods_count--;
}

u32 bootstub_main(u32 magic, struct multiboot_info *mbi)
{
	/* B: entered C successfully. */
	debug_char('B');

	if (magic != MB_BOOTLOADER_MAGIC)
		fail("not entered via Multiboot-1");

	if (mbi == (void *)0)
		fail("NULL Multiboot info");

	if ((mbi->flags & MB_INFO_MODS) == 0)
		fail("QEMU supplied no modules");

	/*
	 * Need at least:
	 *   module 0 = real seL4 kernel
	 *   module 1 = seL4 rootserver / dominit0
	 */
	if (mbi->mods_count < 2)
		fail("need kernel ELF + rootserver modules");

	/* C: Multiboot info and minimum module set look sane. */
	debug_char('C');

	struct multiboot_module *mods =
		(struct multiboot_module *)mbi->mods_addr;

	u32 entry = load_kernel_elf(&mods[0]);

	/* D: ELF validated and every PT_LOAD segment was copied/zeroed. */
	debug_char('D');

	remove_kernel_module(mbi);

	/* E: module 0 removed; remaining arbitrary modules shifted down. */
	debug_char('E');

	return entry;
}
