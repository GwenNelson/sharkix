OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x400000;
    .text : { *(.text*) *(.rodata*) }
    .data : { *(.data*) *(.bss*) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
