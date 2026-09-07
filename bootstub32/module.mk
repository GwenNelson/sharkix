# This module intentionally does not use mk/arch/x86/32.mk. bootstub32 is a
# standalone i386 Multiboot trampoline, not a Sharkix 32-bit kernel port.
BOOTSTUB32_MODULE_ROOT := $(BOOTSTUB32_ROOT)
BOOTSTUB32_BUILD_ROOT = $(CONFIG_BUILD_ROOT)/bootstub32
BOOTSTUB32_ARTIFACT = $(BOOTSTUB32_BUILD_ROOT)/bootstub32
BOOTSTUB32_OBJECTS = $(BOOTSTUB32_BUILD_ROOT)/bootstub32.o $(BOOTSTUB32_BUILD_ROOT)/bootstub32_c.o
BOOTSTUB32_CFLAGS = -m32 -march=i386 -std=gnu11 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-sse -mno-sse2 -mno-mmx -msoft-float -Wall -Wextra -Werror $(if $(filter 1,$(DEBUG)),-O0 -g -DBOOTSTUB_DEBUG_E9,-O2)
BOOTSTUB32_ASFLAGS = -m32 -march=i386 -ffreestanding -fno-pic -fno-pie $(if $(filter 1,$(DEBUG)),-g -DBOOTSTUB_DEBUG_E9)
BOOTSTUB32_LDFLAGS = -m elf_i386 -T $(BOOTSTUB32_MODULE_ROOT)/bootstub32.ld
