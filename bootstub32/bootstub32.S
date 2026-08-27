/*
 * bootstub32.S
 *
 * Tiny Multiboot-1 trampoline for QEMU's x86 -kernel loader.
 *
 * QEMU loads this as a 32-bit Multiboot kernel.  Module 0 is expected
 * to be the real ELF64 kernel.  bootstub_main() loads it, removes that
 * module from the Multiboot module list, then returns its 32-bit entry
 * address.  We jump there with the original Multiboot-1 environment.
 */

.set MULTIBOOT_HEADER_MAGIC, 0x1badb002
.set MULTIBOOT_HEADER_FLAGS, 0x00000003
.set MULTIBOOT_HEADER_CHECKSUM, -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

.set MULTIBOOT_BOOTLOADER_MAGIC, 0x2badb002

.section .multiboot, "a"
.align 4
.long MULTIBOOT_HEADER_MAGIC
.long MULTIBOOT_HEADER_FLAGS
.long MULTIBOOT_HEADER_CHECKSUM

.section .text, "ax"
.code32

.global _start
.type _start, @function
.extern bootstub_main
.extern bootstub_stack_top

/*
 * QEMU/Bochs debug console.
 * Run QEMU with:
 *
 *   -debugcon stdio -global isa-debugcon.iobase=0xe9
 *
 * Writing a byte to port 0xe9 makes it immediately visible.
 */
#ifdef BOOTSTUB_DEBUG_E9
.macro DEBUG_CHAR ch
	pushl %eax
	movb $\ch, %al
	outb %al, $0xe9
	popl %eax
.endm
#else
.macro DEBUG_CHAR ch
.endm
#endif


_start:
	cli

	/* A: reached the very first instructions of bootstub32. */
	DEBUG_CHAR 'A'

	/*
	 * Multiboot does not define ESP.  Switch immediately to our own
	 * stack, which lives inside the stub image.
	 */
	movl $bootstub_stack_top, %esp
	xorl %ebp, %ebp

	/*
	 * Preserve the Multiboot-info pointer in EDI.  EDI is callee-saved
	 * by the i386 SysV ABI used by bootstub_main().
	 *
	 * On entry:
	 *   EAX = Multiboot-1 bootloader magic
	 *   EBX = physical pointer to multiboot_info
	 */
	movl %ebx, %edi

	/* About to enter C. */
	DEBUG_CHAR '>'

	/*
	 * cdecl:
	 *   uint32_t bootstub_main(uint32_t magic, struct mb_info *mbi);
	 *
	 * Push right-to-left.
	 */
	pushl %ebx
	pushl %eax
	call bootstub_main
	addl $8, %esp

	/* <: C returned successfully with the real kernel entry in EAX. */
	DEBUG_CHAR '<'

	/*
	 * bootstub_main() returns the real kernel's 32-bit entry address.
	 * Restore the Multiboot handoff exactly as the real kernel expects.
	 */
	movl %eax, %edx
	movl $MULTIBOOT_BOOTLOADER_MAGIC, %eax
	movl %edi, %ebx

	/* J: final instruction sequence before entering the real kernel. */
	DEBUG_CHAR 'J'

	/*
	 * DEBUG_CHAR changed AL, so restore the Multiboot magic after it.
	 */
	movl $MULTIBOOT_BOOTLOADER_MAGIC, %eax
	jmp *%edx

.size _start, . - _start
