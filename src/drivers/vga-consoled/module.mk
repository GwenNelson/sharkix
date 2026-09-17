
VGA_CONSOLED_DIR := $(SHARKIX_PROJECT_ROOT)/src/drivers/vga-consoled

VGA_CONSOLED_ENTRY_SOURCE  := $(DRIVER_RING3_ENTRY)
VGA_CONSOLED_SOURCE        := $(VGA_CONSOLED_DIR)/user/main.c
VGA_CONSOLED_LINKER_SCRIPT := $(DRIVER_RING3_LINKER)

VGA_CONSOLED_ENTRY_OBJECT  := $(USER_OBJ_ROOT)/vga-consoled/entry.o
VGA_CONSOLED_OBJECT        := $(USER_OBJ_ROOT)/vga-consoled/main.o
VGA_CONSOLED_ELF           := $(USER_OBJ_ROOT)/vga-consoled/vga-consoled.elf
VGA_CONSOLED_BINARY        := $(USER_OBJ_ROOT)/vga-consoled/vga-consoled.bin
VGA_CONSOLED_EMBED_OBJECT  := $(KERNEL_OBJ_ROOT)/user_vga_consoled.o
VGA_CONSOLED_LIBSHARKIX    := $(CONFIG_BUILD_ROOT)/libsharkix/libsharkix-user.a

VGA_CONSOLED_CFLAGS := \
    -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
    -m64 -fno-stack-protector -fno-pic -fno-pie \
    -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
    -fno-asynchronous-unwind-tables
VGA_CONSOLED_ASFLAGS := -x assembler-with-cpp -ffreestanding -m64
VGA_CONSOLED_CPPFLAGS := -I$(INCLUDE_ROOT)

VGA_CONSOLED_BINARY_NAME := $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(VGA_CONSOLED_BINARY))
VGA_CONSOLED_BINARY_SYMBOL := _binary_$(subst /,_,$(subst .,_,$(subst -,_,$(VGA_CONSOLED_BINARY_NAME))))

$(VGA_CONSOLED_LIBSHARKIX): FORCE
	$(MAKE) -C $(SHARKIX_PROJECT_ROOT)/src/libsharkix \
		BUILD_OUT=$(CONFIG_BUILD_ROOT)/libsharkix \
		INCLUDE=$(INCLUDE_ROOT) \
		DEBUG=$(DEBUG) \
		$(VGA_CONSOLED_LIBSHARKIX)

$(VGA_CONSOLED_ENTRY_OBJECT): $(VGA_CONSOLED_ENTRY_SOURCE)
	$(MKDIR_P) $(@D)
	$(CC) $(VGA_CONSOLED_CPPFLAGS) $(VGA_CONSOLED_ASFLAGS) -c $< -o $@

$(VGA_CONSOLED_OBJECT): $(VGA_CONSOLED_SOURCE)
	$(MKDIR_P) $(@D)
	$(CC) $(VGA_CONSOLED_CPPFLAGS) $(VGA_CONSOLED_CFLAGS) -c $< -o $@

$(VGA_CONSOLED_ELF): $(VGA_CONSOLED_ENTRY_OBJECT) $(VGA_CONSOLED_OBJECT) $(VGA_CONSOLED_LINKER_SCRIPT) $(VGA_CONSOLED_LIBSHARKIX)
	$(MKDIR_P) $(@D)
	$(LD) -m $(USER_LINKER_FORMAT) -nostdlib -o $@ \
		$(VGA_CONSOLED_ENTRY_OBJECT) $(VGA_CONSOLED_OBJECT) $(VGA_CONSOLED_LIBSHARKIX) \
		-T $(VGA_CONSOLED_LINKER_SCRIPT)

$(VGA_CONSOLED_BINARY): $(VGA_CONSOLED_ELF)
	$(OBJCOPY) -O binary $< $@

$(VGA_CONSOLED_EMBED_OBJECT): $(VGA_CONSOLED_BINARY)
	$(MKDIR_P) $(@D)
	cd $(SHARKIX_PROJECT_ROOT) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(VGA_CONSOLED_BINARY_SYMBOL)_start=vga_consoled_image_start \
		--redefine-sym $(VGA_CONSOLED_BINARY_SYMBOL)_end=vga_consoled_image_end \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$<) \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$@)

KERNEL_C_SRCS += $(SHARKIX_PROJECT_ROOT)/src/kernel/../drivers/vga-consoled/kernel/console-vga.c 

USER_OBJECTS += $(VGA_CONSOLED_EMBED_OBJECT)
