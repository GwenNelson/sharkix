MAKEFILE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
SHARKIX_ROOT ?= $(abspath $(MAKEFILE_DIR)/../..)

LIBSHARKIX_SRC ?= $(SHARKIX_ROOT)/src/libsharkix

CC ?= gcc
AR ?= ar

BUILD_OUT ?= $(SHARKIX_ROOT)/build/libsharkix

INCLUDE ?= $(SHARKIX_ROOT)/include

CPPFLAGS := -I$(INCLUDE)


CFLAGS := \
	-std=gnu11 \
	-ffreestanding \
	-Wall \
	-Wextra \
	-m64 \
	-fno-stack-protector \
	-mno-red-zone \
	-fPIE \
	-mno-sse \
	-mno-mmx \
	-mno-80387

ASFLAGS := \
	-x assembler-with-cpp \
	-ffreestanding \
	-m64

# Optimization and debug flags
ifeq ($(DEBUG),1)
    CFLAGS += -Og -g
else
    CFLAGS += -O2
endif

OBJECTS := $(BUILD_OUT)/syscall-asm-stub.o $(BUILD_OUT)/syscall-c.o
ARTIFACT := $(BUILD_OUT)/libsharkix.a

.PHONY: all clean

all: $(ARTIFACT)

$(BUILD_OUT)/%.o: $(LIBSHARKIX_SRC)/%.S
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -c $< -o $@

$(BUILD_OUT)/%.o: $(LIBSHARKIX_SRC)/%.c
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(ARTIFACT): $(OBJECTS)
	$(AR) rcs $@ $^

clean:
	rm -rf $(BUILD_OUT)

-include $(OBJECTS:.o=.d)
