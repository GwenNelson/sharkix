.text
.code64
.global _start
_start:
    movw $0x3fd, %dx
1:  inb %dx, %al
    testb $0x20, %al
    jz 1b
    movw $0x3f8, %dx
    movb $'B', %al
    outb %al, %dx
    int $0x21
    movw $0x3fd, %dx
    jmp 1b
