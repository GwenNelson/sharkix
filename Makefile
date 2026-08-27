CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy
GRUB_MKRESCUE ?= grub-mkrescue
QEMU ?= qemu-system-x86_64
QEMU_ACCEL_FLAGS ?= $(if $(wildcard /dev/kvm),-enable-kvm -cpu host,-cpu qemu64)
MKDIR_P ?= mkdir -p
BUILD_DIR := build
USER_ELFS := $(BUILD_DIR)/user/taskA.elf $(BUILD_DIR)/user/taskB.elf

CFLAGS := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m64 -mcmodel=kernel \
 -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
 -fno-asynchronous-unwind-tables
ASFLAGS := -x assembler-with-cpp -ffreestanding -m64
INCLUDES := -I. -Ikernel/freertos/include -Ikernel/arch/x86_64 -Ikernel
LDFLAGS := -m elf_x86_64 -T kernel/linker.ld -nostdlib
KERNEL_OBJS := kernel/boot.o kernel/main.o kernel/libc.o kernel/memory.o \
 kernel/task_loader.o kernel/user_taskA.o kernel/user_taskB.o \
 kernel/freertos/tasks.o kernel/freertos/queue.o kernel/freertos/list.o \
 kernel/freertos/event_groups.o kernel/freertos/stream_buffer.o \
 kernel/freertos/croutine.o kernel/freertos/heap_4.o kernel/arch/x86_64/port.o \
 kernel/arch/x86_64/portASM.o

.PHONY: all clean iso run run-iso verify
.SECONDARY: $(USER_ELFS)
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

$(BUILD_DIR)/user/%.o: user/%.s
	$(MKDIR_P) $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD_DIR)/user/%.elf: $(BUILD_DIR)/user/%.o user/task.ld
	$(LD) -m elf_x86_64 -T user/task.ld -nostdlib -o $@ $<
$(BUILD_DIR)/user/%.bin: $(BUILD_DIR)/user/%.elf
	$(OBJCOPY) -O binary $< $@
kernel/user_taskA.o: $(BUILD_DIR)/user/taskA.bin
	$(LD) -r -b binary -m elf_x86_64 -o $@ $<
	$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	 --redefine-sym _binary_build_user_taskA_bin_start=taskA_image_start \
	 --redefine-sym _binary_build_user_taskA_bin_end=taskA_image_end $@
kernel/user_taskB.o: $(BUILD_DIR)/user/taskB.bin
	$(LD) -r -b binary -m elf_x86_64 -o $@ $<
	$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	 --redefine-sym _binary_build_user_taskB_bin_start=taskB_image_start \
	 --redefine-sym _binary_build_user_taskB_bin_end=taskB_image_end $@

bootstub32/bootstub32:
	$(MAKE) -C bootstub32
run: kernel.elf bootstub32/bootstub32
	$(QEMU) $(QEMU_ACCEL_FLAGS) -m 512M -kernel ./bootstub32/bootstub32 -initrd kernel.elf -serial stdio -display none -no-reboot
iso: kernel.elf
	$(MKDIR_P) iso/boot/grub
	cp kernel.elf iso/boot/kernel.elf
	cp grub.cfg iso/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o sharkix.iso iso >/dev/null
run-iso: iso
	$(QEMU) $(QEMU_ACCEL_FLAGS) -m 512M -cdrom sharkix.iso -serial stdio -display none -no-reboot
verify: kernel.elf
	readelf -h kernel.elf
	readelf -l kernel.elf
	grub-file --is-x86-multiboot kernel.elf
clean:
	rm -f $(KERNEL_OBJS) kernel.elf sharkix.iso
	rm -rf iso $(BUILD_DIR)
