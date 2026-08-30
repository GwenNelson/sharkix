OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x0000000000400000;
    .text : { *(.text .text.*) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
