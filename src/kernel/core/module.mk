KERNEL_CORE_ROOT := $(KERNEL_SRC_ROOT)/core

KERNEL_CORE_C_SRCS := \
 $(KERNEL_CORE_ROOT)/console.c $(KERNEL_CORE_ROOT)/ipc_registry.c \
 $(KERNEL_CORE_ROOT)/kvalloc.c $(KERNEL_CORE_ROOT)/libc.c \
 $(KERNEL_CORE_ROOT)/main.c $(KERNEL_CORE_ROOT)/memory.c \
 $(KERNEL_CORE_ROOT)/program.c $(KERNEL_CORE_ROOT)/scheduler.c \
 $(KERNEL_CORE_ROOT)/sync.c $(KERNEL_CORE_ROOT)/syscall.c \
 $(KERNEL_CORE_ROOT)/thread.c
