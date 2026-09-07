SHARKIX_PROJECT_ROOT := $(abspath $(dir $(SHARKIX_TOPLEVEL_MAKEFILE)))

KERNEL_SRC_ROOT ?= $(SHARKIX_PROJECT_ROOT)/src/kernel
USER_SRC_ROOT ?= $(SHARKIX_PROJECT_ROOT)/src/user
INCLUDE_ROOT ?= $(SHARKIX_PROJECT_ROOT)/include
KERNEL_INCLUDE_ROOT ?= $(INCLUDE_ROOT)/sharkix/kernel
USER_INCLUDE_ROOT ?= $(INCLUDE_ROOT)
MK_ROOT ?= $(SHARKIX_PROJECT_ROOT)/mk
CONFIG_ROOT ?= $(SHARKIX_PROJECT_ROOT)/configs
BUILD_ROOT ?= $(SHARKIX_PROJECT_ROOT)/build
EXTERNAL_ROOT ?= $(SHARKIX_PROJECT_ROOT)/external
BOOTSTUB32_ROOT ?= $(SHARKIX_PROJECT_ROOT)/bootstub32

CONFIG ?= pc-x86_64-debug
PROFILE ?= normal
DEBUG ?= 0

CC ?= gcc
LD ?= ld
AR ?= ar
OBJCOPY ?= objcopy
GRUB_MKRESCUE ?= grub-mkrescue
QEMU ?= qemu-system-x86_64
MKDIR_P ?= mkdir -p
QEMU_ACCEL_FLAGS ?= $(if $(wildcard /dev/kvm),-enable-kvm -cpu host,-cpu qemu64)

BUILD_VARIANT = $(if $(filter 1,$(DEBUG)),debug,release)
CONFIG_BUILD_ROOT = $(BUILD_ROOT)/$(CONFIG)/$(PROFILE)/$(BUILD_VARIANT)
KERNEL_OBJ_ROOT = $(CONFIG_BUILD_ROOT)/kernel
USER_OBJ_ROOT = $(CONFIG_BUILD_ROOT)/user
KERNEL_ELF = $(CONFIG_BUILD_ROOT)/kernel.elf
ISO_ROOT = $(CONFIG_BUILD_ROOT)/iso
ISO_IMAGE = $(CONFIG_BUILD_ROOT)/sharkix.iso

# Module accumulators. Modules append; only documented scalar selections are
# assigned by the selector that owns them.
KERNEL_C_SRCS :=
KERNEL_ASM_SRCS :=
KERNEL_CPPFLAGS :=
KERNEL_CFLAGS = -std=gnu11 -ffreestanding $(if $(filter 1,$(DEBUG)),-O0 -g,-O2) -Wall -Wextra
KERNEL_ASFLAGS := -x assembler-with-cpp -ffreestanding
KERNEL_LDFLAGS = -m $(KERNEL_LINKER_FORMAT) -T $(KERNEL_LINKER_SCRIPT) -nostdlib
USER_ASM_SRCS :=
USER_ASFLAGS :=
USER_LDFLAGS = -m $(USER_LINKER_FORMAT) -T $(USER_LINKER_SCRIPT) -nostdlib
USER_EMBED_SPECS :=
KERNEL_LINKER_SCRIPT :=
KERNEL_LINKER_FORMAT :=

KERNEL_OBJECTS :=
USER_OBJECTS :=
KERNEL_DEPFILES :=
BOOT_ARTIFACTS :=
QEMU_ARGS :=
PROFILE_SOURCE :=
