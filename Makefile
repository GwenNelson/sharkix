CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy
GRUB_MKRESCUE ?= grub-mkrescue
QEMU ?= qemu-system-x86_64
QEMU_ACCEL_FLAGS ?= $(if $(wildcard /dev/kvm),-enable-kvm -cpu host,-cpu qemu64)
MKDIR_P ?= mkdir -p
BUILD_DIR := build
PROFILE ?= normal
PROFILES := normal syscall exceptions vm lifecycle two_tasks_one_space syscall_block

ifeq ($(filter $(PROFILE),$(PROFILES)),)
$(error unknown PROFILE '$(PROFILE)'; choose one of $(PROFILES))
endif

CFLAGS := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m64 -mcmodel=kernel \
 -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
 -fno-asynchronous-unwind-tables
ASFLAGS := -x assembler-with-cpp -ffreestanding -m64
INCLUDES := -I. -Ikernel/freertos/include -Ikernel/arch/x86_64 -Ikernel
LDFLAGS := -m elf_x86_64 -T kernel/linker.ld -nostdlib
USER_TASKS := taskA taskB tests/ud tests/pagefault tests/kernel_access tests/exit tests/syscall_blocker tests/syscall_waker
USER_ELFS := $(addprefix $(BUILD_DIR)/user/,$(addsuffix .elf,$(USER_TASKS)))
USER_OBJS := kernel/user_taskA.o kernel/user_taskB.o kernel/user_ud.o kernel/user_pagefault.o kernel/user_kernel_access.o kernel/user_exit.o kernel/user_syscall_blocker.o kernel/user_syscall_waker.o
KERNEL_OBJS := kernel/boot.o kernel/main.o kernel/libc.o kernel/memory.o kernel/thread.o kernel/program.o kernel/syscall.o \
 kernel/startup/common.o kernel/startup/$(PROFILE).o $(USER_OBJS) \
 kernel/freertos/tasks.o kernel/freertos/queue.o kernel/freertos/list.o \
 kernel/freertos/event_groups.o kernel/freertos/stream_buffer.o kernel/freertos/croutine.o \
 kernel/freertos/heap_4.o kernel/arch/x86_64/port.o kernel/arch/x86_64/portASM.o

.PHONY: all clean iso run run-iso verify FORCE
.SECONDARY: $(USER_ELFS)
all: kernel.elf
kernel.elf: FORCE $(KERNEL_OBJS) kernel/linker.ld FreeRTOSConfig.h
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)
FORCE:
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

define EMBED_RULE
kernel/user_$(1).o: $(BUILD_DIR)/user/$(2).bin
	$(LD) -r -b binary -m elf_x86_64 -o $$@ $$<
	$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	 --redefine-sym _binary_build_user_$(subst /,_,$(2))_bin_start=$(3)_image_start \
	 --redefine-sym _binary_build_user_$(subst /,_,$(2))_bin_end=$(3)_image_end $$@
endef
$(eval $(call EMBED_RULE,taskA,taskA,taskA))
$(eval $(call EMBED_RULE,taskB,taskB,taskB))
$(eval $(call EMBED_RULE,ud,tests/ud,ud))
$(eval $(call EMBED_RULE,pagefault,tests/pagefault,pagefault))
$(eval $(call EMBED_RULE,kernel_access,tests/kernel_access,kernel_access))
$(eval $(call EMBED_RULE,exit,tests/exit,exit))
$(eval $(call EMBED_RULE,syscall_blocker,tests/syscall_blocker,syscall_blocker))
$(eval $(call EMBED_RULE,syscall_waker,tests/syscall_waker,syscall_waker))

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
	rm -f $(KERNEL_OBJS) $(USER_OBJS) kernel.elf sharkix.iso
	rm -rf iso $(BUILD_DIR)
