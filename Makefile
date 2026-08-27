CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy
GRUB_MKRESCUE ?= grub-mkrescue
MKDIR_P ?= mkdir -p

ARCH := i386
CFLAGS := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pic -m32 -mno-sse -mno-mmx -mno-red-zone -mno-80387 -fno-asynchronous-unwind-tables
ASFLAGS := -x assembler-with-cpp -ffreestanding -m32
LDFLAGS := -m elf_i386 -T kernel/linker.ld -nostdlib
INCLUDES := -I. -Ikernel/freertos/include -Ikernel/arch/x86_64

KERNEL_OBJS := \
	kernel/boot.o \
	kernel/main.o \
	kernel/libc.o \
	kernel/freertos/tasks.o \
	kernel/freertos/queue.o \
	kernel/freertos/list.o \
	kernel/freertos/timers.o \
	kernel/freertos/event_groups.o \
	kernel/freertos/stream_buffer.o \
	kernel/freertos/croutine.o \
	kernel/freertos/heap_4.o \
	kernel/arch/x86_64/port.o \
	kernel/arch/x86_64/portASM.o

.PHONY: all clean iso run

all: kernel.elf

kernel.elf: $(KERNEL_OBJS) kernel/linker.ld FreeRTOSConfig.h
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

kernel/%.o: kernel/%.c FreeRTOSConfig.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

kernel/%.o: kernel/%.S FreeRTOSConfig.h
	$(CC) $(ASFLAGS) $(INCLUDES) -c $< -o $@

kernel/arch/x86_64/%.o: kernel/arch/x86_64/%.c FreeRTOSConfig.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

kernel/arch/x86_64/%.o: kernel/arch/x86_64/%.S FreeRTOSConfig.h
	$(CC) $(ASFLAGS) $(INCLUDES) -c $< -o $@

iso: kernel.elf
	$(MKDIR_P) iso/boot/grub
	cp kernel.elf iso/boot/kernel.elf
	cp grub.cfg iso/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o sharkix.iso iso >/dev/null

run: iso
	qemu-system-i386 -cdrom sharkix.iso -serial stdio

clean:
	rm -f $(KERNEL_OBJS) kernel.elf sharkix.iso
	rm -rf iso
