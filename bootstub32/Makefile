# Tiny standalone build for bootstub32.

CC ?= gcc
LD ?= ld


CFLAGS := \
	-m32 \
	-march=i386 \
	-std=gnu11 \
	-ffreestanding \
	-fno-pic \
	-fno-pie \
	-fno-stack-protector \
	-fno-asynchronous-unwind-tables \
	-fno-unwind-tables \
	-mno-sse \
	-mno-sse2 \
	-mno-mmx \
	-msoft-float \
	-Wall \
	-Wextra \
	-Werror

ASFLAGS := \
	-m32 \
	-march=i386 \
	-ffreestanding \
	-fno-pic \
	-fno-pie


ifeq ($(DEBUG),1)
	CFLAGS += -O0 -g -DBOOTSTUB_DEBUG_E9
	ASFLAGS += -g -DBOOTSTUB_DEBUG_E9
else
	CFLAGS += -O2
endif


all: bootstub32

bootstub32.o: bootstub32.S
	$(CC) $(ASFLAGS) -c $< -o $@

bootstub32_c.o: bootstub32.c
	$(CC) $(CFLAGS) -c $< -o $@

bootstub32: bootstub32.o bootstub32_c.o bootstub32.ld
	$(LD) -m elf_i386 -T bootstub32.ld \
		-o $@ bootstub32.o bootstub32_c.o

clean:
	rm -f bootstub32 bootstub32.o bootstub32_c.o

.PHONY: all clean
