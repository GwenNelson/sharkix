#include "FreeRTOSConfig.h"

.set MULTIBOOT_MAGIC, 0x1BADB002
.set MULTIBOOT_FLAGS, 0
.set MULTIBOOT_CHECKSUM, -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)
.set PTE_PRESENT_WRITE, 0x003
.set PTE_HUGE, 0x080

.section .bootstrap, "ax"
.code32
.align 4
.long MULTIBOOT_MAGIC
.long MULTIBOOT_FLAGS
.long MULTIBOOT_CHECKSUM

.global _start
.type _start, @function
.extern kernel_high_entry

_start:
	cli
	movl %eax, bootstrap_magic
	movl %ebx, bootstrap_mbi
	movl $bootstrap_stack_top, %esp
	xorl %ebp, %ebp

	/* PML4[0] is temporary identity mapping, [256] is PHYSMAP,
	 * [384] reserves KHEAP, and [511] maps the kernel image. */
	movl $bootstrap_pdpt_low, %eax
	orl $PTE_PRESENT_WRITE, %eax
	movl %eax, bootstrap_pml4 + (0 * 8)
	movl $0, bootstrap_pml4 + (0 * 8) + 4
	movl $bootstrap_pdpt_phys, %eax
	orl $PTE_PRESENT_WRITE, %eax
	movl %eax, bootstrap_pml4 + (256 * 8)
	movl $0, bootstrap_pml4 + (256 * 8) + 4
	movl $bootstrap_pdpt_heap, %eax
	orl $PTE_PRESENT_WRITE, %eax
	movl %eax, bootstrap_pml4 + (384 * 8)
	movl $0, bootstrap_pml4 + (384 * 8) + 4
	movl $bootstrap_pdpt_kernel, %eax
	orl $PTE_PRESENT_WRITE, %eax
	movl %eax, bootstrap_pml4 + (511 * 8)
	movl $0, bootstrap_pml4 + (511 * 8) + 4

	movl $bootstrap_pd_low, %eax
	orl $PTE_PRESENT_WRITE, %eax
	movl %eax, bootstrap_pdpt_low
	movl $0, bootstrap_pdpt_low + 4
	movl $bootstrap_pd_phys, %eax
	orl $PTE_PRESENT_WRITE, %eax
	movl %eax, bootstrap_pdpt_phys
	movl $0, bootstrap_pdpt_phys + 4
	movl $bootstrap_pd_kernel, %eax
	orl $PTE_PRESENT_WRITE, %eax
	movl %eax, bootstrap_pdpt_kernel + (510 * 8)
	movl $0, bootstrap_pdpt_kernel + (510 * 8) + 4

	xorl %ecx, %ecx
1:
	movl %ecx, %eax
	shll $21, %eax
	orl $(PTE_PRESENT_WRITE | PTE_HUGE), %eax
	movl %eax, bootstrap_pd_low(, %ecx, 8)
	movl $0, bootstrap_pd_low + 4(, %ecx, 8)
	movl %eax, bootstrap_pd_phys(, %ecx, 8)
	movl $0, bootstrap_pd_phys + 4(, %ecx, 8)
	incl %ecx
	cmpl $512, %ecx
	jne 1b

	/* The higher-half image is loaded at physical 0x00200000. */
	xorl %ecx, %ecx
2:
	movl %ecx, %eax
	shll $21, %eax
	addl $0x00200000, %eax
	orl $(PTE_PRESENT_WRITE | PTE_HUGE), %eax
	movl %eax, bootstrap_pd_kernel(, %ecx, 8)
	movl $0, bootstrap_pd_kernel + 4(, %ecx, 8)
	incl %ecx
	cmpl $512, %ecx
	jne 2b

	movl %cr4, %eax
	orl $0x20, %eax
	movl %eax, %cr4
	movl $bootstrap_pml4, %eax
	movl %eax, %cr3
	movl $0xc0000080, %ecx
	rdmsr
	orl $0x100, %eax
	wrmsr
	movl %cr0, %eax
	orl $0x80000001, %eax
	movl %eax, %cr0

	lgdt bootstrap_gdt_pointer
	ljmp $0x18, $long_mode_entry

.code64
.type long_mode_entry, @function
long_mode_entry:
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movabs $stack_top, %rsp
	xorl %ebp, %ebp
	movl bootstrap_magic(%rip), %edi
	movl bootstrap_mbi(%rip), %esi
	movabs $kernel_high_entry, %rax
	call *%rax
3:	hlt
	jmp 3b

.align 8
bootstrap_gdt:
	.quad 0x0000000000000000
	.quad 0x00cf9a000000ffff
	.quad 0x00cf92000000ffff
	.quad 0x00af9a000000ffff
bootstrap_gdt_pointer:
	.word . - bootstrap_gdt - 1
	.long bootstrap_gdt

.section .bootstrap.bss, "aw", @nobits
.align 4096
.global bootstrap_pml4
bootstrap_pml4:
	.skip 4096
bootstrap_pdpt_low:
	.skip 4096
bootstrap_pd_low:
	.skip 4096
bootstrap_pdpt_phys:
	.skip 4096
bootstrap_pd_phys:
	.skip 4096
bootstrap_pdpt_heap:
	.skip 4096
bootstrap_pdpt_kernel:
	.skip 4096
bootstrap_pd_kernel:
	.skip 4096

.align 16
bootstrap_magic:
	.skip 4
bootstrap_mbi:
	.skip 4
bootstrap_stack_bottom:
	.skip 16384
bootstrap_stack_top:

.section .bss, "aw", @nobits
.align 16
stack_bottom:
	.skip 32768
stack_top:
