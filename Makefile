CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy
GRUB_MKRESCUE ?= grub-mkrescue
QEMU ?= qemu-system-x86_64
QEMU_ACCEL_FLAGS ?= $(if $(wildcard /dev/kvm),-enable-kvm -cpu host,-cpu qemu64)
MKDIR_P ?= mkdir -p
BUILD_DIR := build
INCLUDE_DIR := include
KERNEL_SRC_DIR := src/kernel
USER_SRC_DIR := src/user
KERNEL_BUILD_DIR := $(BUILD_DIR)/kernel
USER_BUILD_DIR := $(BUILD_DIR)/user
PROFILE ?= normal
PROFILES := normal syscall exceptions vm lifecycle two_tasks_one_space syscall_block testipc preemption user_ipc
LIBFIFO_A := external/libfifo/build/libfifo.a

ifeq ($(filter $(PROFILE),$(PROFILES)),)
$(error unknown PROFILE '$(PROFILE)'; choose one of $(PROFILES))
endif

CFLAGS := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m64 -mcmodel=kernel \
 -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
 -fno-asynchronous-unwind-tables
ASFLAGS := -x assembler-with-cpp -ffreestanding -m64
DEBUG_CFLAGS := -std=gnu11 -ffreestanding -O0 -g -Wall -Wextra -m64 -mcmodel=kernel \
 -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
 -fno-asynchronous-unwind-tables
DEBUG_ASFLAGS := -x assembler-with-cpp -ffreestanding -m64 -g
DEPFLAGS := -MMD -MP
INCLUDES := -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/sharkix/kernel/freertos \
 -I$(INCLUDE_DIR)/sharkix/kernel/arch/x86_64 -I$(INCLUDE_DIR)/sharkix/kernel \
 -Iexternal/libfifo/include
LDFLAGS := -m elf_x86_64 -T $(KERNEL_SRC_DIR)/linker.ld -nostdlib
USER_TASKS := taskA taskB tests/ud tests/pagefault tests/kernel_access tests/exit tests/syscall_blocker tests/syscall_waker task_IPC_consumer task_IPC_producer
USER_ASM_OBJS := $(addprefix $(USER_BUILD_DIR)/,$(addsuffix .o,$(USER_TASKS)))
USER_ELFS := $(addprefix $(USER_BUILD_DIR)/,$(addsuffix .elf,$(USER_TASKS)))
USER_OBJS := $(KERNEL_BUILD_DIR)/user_taskA.o $(KERNEL_BUILD_DIR)/user_taskB.o $(KERNEL_BUILD_DIR)/user_ud.o $(KERNEL_BUILD_DIR)/user_pagefault.o $(KERNEL_BUILD_DIR)/user_kernel_access.o $(KERNEL_BUILD_DIR)/user_exit.o $(KERNEL_BUILD_DIR)/user_syscall_blocker.o $(KERNEL_BUILD_DIR)/user_syscall_waker.o $(KERNEL_BUILD_DIR)/user_ipc_consumer.o $(KERNEL_BUILD_DIR)/user_ipc_producer.o
KERNEL_OBJS := $(KERNEL_BUILD_DIR)/boot.o $(KERNEL_BUILD_DIR)/main.o $(KERNEL_BUILD_DIR)/libc.o $(KERNEL_BUILD_DIR)/memory.o $(KERNEL_BUILD_DIR)/thread.o $(KERNEL_BUILD_DIR)/program.o $(KERNEL_BUILD_DIR)/syscall.o \
 $(KERNEL_BUILD_DIR)/startup/common.o $(KERNEL_BUILD_DIR)/startup/$(PROFILE).o $(USER_OBJS) \
 $(KERNEL_BUILD_DIR)/freertos/tasks.o $(KERNEL_BUILD_DIR)/freertos/queue.o $(KERNEL_BUILD_DIR)/freertos/list.o \
 $(KERNEL_BUILD_DIR)/freertos/event_groups.o $(KERNEL_BUILD_DIR)/freertos/stream_buffer.o $(KERNEL_BUILD_DIR)/freertos/croutine.o \
 $(KERNEL_BUILD_DIR)/freertos/heap_4.o $(KERNEL_BUILD_DIR)/arch/x86_64/port.o $(KERNEL_BUILD_DIR)/arch/x86_64/portASM.o $(KERNEL_BUILD_DIR)/ipc.o

.PHONY: all clean iso run run-gdb run-iso verify FORCE
.SECONDARY: $(USER_ASM_OBJS) $(USER_ELFS)
.DEFAULT_GOAL := all
-include $(KERNEL_OBJS:.o=.d)
all: kernel.elf
kernel.elf: FORCE $(KERNEL_OBJS) $(LIBFIFO_A) $(KERNEL_SRC_DIR)/linker.ld $(INCLUDE_DIR)/FreeRTOSConfig.h
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS) $(LIBFIFO_A)
FORCE:

$(LIBFIFO_A):
	$(MAKE) -C external/libfifo CFLAGS='$(CFLAGS)' build/libfifo.a

$(KERNEL_BUILD_DIR)/%.o: $(KERNEL_SRC_DIR)/%.c $(INCLUDE_DIR)/FreeRTOSConfig.h
	$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

$(KERNEL_BUILD_DIR)/%.o: $(KERNEL_SRC_DIR)/%.S $(INCLUDE_DIR)/FreeRTOSConfig.h
	$(MKDIR_P) $(dir $@)
	$(CC) $(ASFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

$(USER_BUILD_DIR)/%.o: $(USER_SRC_DIR)/%.s
	$(MKDIR_P) $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@
$(USER_BUILD_DIR)/%.elf: $(USER_BUILD_DIR)/%.o $(USER_SRC_DIR)/task.ld
	$(LD) -m elf_x86_64 -T $(USER_SRC_DIR)/task.ld -nostdlib -o $@ $<
$(USER_BUILD_DIR)/%.bin: $(USER_BUILD_DIR)/%.elf
	$(OBJCOPY) -O binary $< $@

define EMBED_RULE
$(KERNEL_BUILD_DIR)/user_$(1).o: $(USER_BUILD_DIR)/$(2).bin
	$(MKDIR_P) $$(dir $$@)
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
$(eval $(call EMBED_RULE,ipc_consumer,task_IPC_consumer,ipc_consumer))
$(eval $(call EMBED_RULE,ipc_producer,task_IPC_producer,ipc_producer))

bootstub32/bootstub32:
	$(MAKE) -C bootstub32
run: kernel.elf bootstub32/bootstub32
	@$(QEMU) $(QEMU_ACCEL_FLAGS) -m 512M -kernel ./bootstub32/bootstub32 -initrd kernel.elf -serial stdio -display none -no-reboot; \
	status=$$?; \
	echo; \
	echo "QEMU exit status: $$status"; \
	exit $$status
run-gdb:
	$(MAKE) clean
	$(MAKE) CFLAGS='$(DEBUG_CFLAGS)' ASFLAGS='$(DEBUG_ASFLAGS)' kernel.elf
	$(MAKE) -C bootstub32 clean
	$(MAKE) -C bootstub32 DEBUG=1
	$(QEMU) $(QEMU_ACCEL_FLAGS) -m 512M -kernel ./bootstub32/bootstub32 -initrd kernel.elf -serial stdio -display none -no-reboot -S -s

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
	make -C external/libfifo clean
