MAKEFILE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
SHARKIX_ROOT ?= $(abspath $(MAKEFILE_DIR)/../..)

LIBSHARKIX_SRC ?= $(SHARKIX_ROOT)/src/libsharkix

CC ?= gcc
AR ?= ar

BUILD_OUT ?= $(SHARKIX_ROOT)/build/libsharkix
INCLUDE ?= $(SHARKIX_ROOT)/include

CPPFLAGS += -I$(INCLUDE)

COMMON_CFLAGS := \
	-std=gnu11 \
	-ffreestanding \
	-Wall \
	-Wextra \
	-m64 \
	-fno-stack-protector \
	-mno-red-zone \
	-mno-sse \
	-mno-mmx \
	-mno-80387

USER_CFLAGS := $(COMMON_CFLAGS) -fPIE
KERNEL_CFLAGS := $(COMMON_CFLAGS) -fno-pic -fno-pie -mcmodel=kernel

USER_ASFLAGS := \
	-x assembler-with-cpp \
	-ffreestanding \
	-m64

KERNEL_ASFLAGS := \
	-x assembler-with-cpp \
	-ffreestanding \
	-m64

ifeq ($(DEBUG),1)
	USER_CFLAGS += -Og -g
	KERNEL_CFLAGS += -Og -g
	USER_ASFLAGS += -g
	KERNEL_ASFLAGS += -g
else
	USER_CFLAGS += -O2
	KERNEL_CFLAGS += -O2
endif

USER_OBJECTS := \
	$(BUILD_OUT)/user/syscall-c.o \
	$(BUILD_OUT)/user/syscall-asm-stub.o

KERNEL_OBJECTS := \
	$(BUILD_OUT)/kernel/syscall-c.o \
	$(BUILD_OUT)/kernel/syscall-kernel-stub.o

USER_ARTIFACT := $(BUILD_OUT)/libsharkix-user.a
KERNEL_ARTIFACT := $(BUILD_OUT)/libsharkix-kernel.a

.PHONY: all clean

all: $(USER_ARTIFACT) $(KERNEL_ARTIFACT)

$(BUILD_OUT)/user/%.o: $(LIBSHARKIX_SRC)/%.c
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(USER_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_OUT)/user/%.o: $(LIBSHARKIX_SRC)/%.S
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(USER_ASFLAGS) -MMD -MP -c $< -o $@

$(BUILD_OUT)/kernel/%.o: $(LIBSHARKIX_SRC)/%.c
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(KERNEL_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_OUT)/kernel/%.o: $(LIBSHARKIX_SRC)/%.S
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(KERNEL_ASFLAGS) -MMD -MP -c $< -o $@

$(USER_ARTIFACT): $(USER_OBJECTS)
	mkdir -p $(@D)
	$(AR) rcs $@ $^

$(KERNEL_ARTIFACT): $(KERNEL_OBJECTS)
	mkdir -p $(@D)
	$(AR) rcs $@ $^

clean:
	rm -rf $(BUILD_OUT)

-include $(USER_OBJECTS:.o=.d)
-include $(KERNEL_OBJECTS:.o=.d)
