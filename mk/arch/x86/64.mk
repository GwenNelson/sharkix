include $(MK_ROOT)/arch/x86/common.mk

ifeq ($(ARCH_BITS),64)
else
$(error x86-64 selection loaded for ARCH_BITS=$(ARCH_BITS))
endif

KERNEL_CFLAGS += -m64 -mcmodel=kernel -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mno-sse -mno-mmx -mno-80387 -fno-asynchronous-unwind-tables
KERNEL_ASFLAGS += -m64 $(if $(filter 1,$(DEBUG)),-g)
KERNEL_CPPFLAGS += -I$(INCLUDE_ROOT) -I$(KERNEL_INCLUDE_ROOT)/freertos -I$(KERNEL_INCLUDE_ROOT)/arch/x86_64 -I$(KERNEL_INCLUDE_ROOT) -I$(EXTERNAL_ROOT)/libfifo/include
KERNEL_LINKER_FORMAT := elf_x86_64
USER_ASFLAGS += -x assembler-with-cpp -ffreestanding -m64
USER_LINKER_FORMAT := elf_x86_64
