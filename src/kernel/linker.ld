OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_BASE = 0xffffffff80000000;
PHYSMAP_BASE = 0xffff800000000000;
KHEAP_BASE = 0xffffc00000000000;

SECTIONS
{
    . = 0x00100000;
    .bootstrap ALIGN(4K) : AT(0x00100000)
    {
        KEEP(*(.bootstrap))
    }
    .bootstrap.bss ALIGN(4K) (NOLOAD) :
    {
        KEEP(*(.bootstrap.bss))
    }

    . = KERNEL_BASE;
    __kernel_virtual_start = .;
    .text ALIGN(4K) : AT(0x00200000)
    {
        *(.text .text.*)
    }
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_BASE + 0x00200000)
    {
        *(.rodata .rodata.*)
    }
    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_BASE + 0x00200000)
    {
        *(.data .data.*)
    }
    .bss ALIGN(4K) (NOLOAD) : AT(ADDR(.bss) - KERNEL_BASE + 0x00200000)
    {
        *(COMMON)
        *(.bss .bss.*)
    }
    __kernel_virtual_end = .;
    __kernel_physical_start = LOADADDR(.text);
    __kernel_physical_end = ADDR(.bss) - KERNEL_BASE + 0x00200000 + SIZEOF(.bss);

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
