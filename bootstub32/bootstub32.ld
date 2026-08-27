/*
 * bootstub32.ld
 *
 * Keep the stub well above seL4's normal low physical kernel image.
 * QEMU places -initrd Multiboot modules after the loaded stub image,
 * so the ELF64 kernel source and arbitrary modules also end up above here.
 */

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
	. = 0x02000000;
	__stub_start = .;

	/*
	 * QEMU scans the first 8192 bytes of the kernel image for the
	 * Multiboot-1 header.  Keep it first.
	 */
	.multiboot ALIGN(4) : {
		KEEP(*(.multiboot))
	}

	.text ALIGN(16) : {
		*(.text .text.*)
	}

	.rodata ALIGN(16) : {
		*(.rodata .rodata.*)
	}

	.data ALIGN(16) : {
		*(.data .data.*)
	}

	.bss ALIGN(16) (NOLOAD) : {
		*(COMMON)
		*(.bss .bss.*)

		. = ALIGN(16);
		bootstub_stack_bottom = .;
		. += 16384;
		bootstub_stack_top = .;
	}

	. = ALIGN(4096);
	__stub_end = .;

	/DISCARD/ : {
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}
}
