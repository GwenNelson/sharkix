SERIAL_CONSOLED_DIR := $(SHARKIX_PROJECT_ROOT)/src/drivers/serial-consoled

SERIAL_CONSOLED_ENTRY_SOURCE  := $(SERIAL_CONSOLED_DIR)/user/entry.S
SERIAL_CONSOLED_SOURCE        := $(SERIAL_CONSOLED_DIR)/user/main.c
SERIAL_CONSOLED_LINKER_SCRIPT := $(DRIVER_RING3_LINKER)

SERIAL_CONSOLED_ENTRY_OBJECT  := $(USER_OBJ_ROOT)/serial-consoled/entry.o
SERIAL_CONSOLED_OBJECT        := $(USER_OBJ_ROOT)/serial-consoled/main.o
SERIAL_CONSOLED_ELF           := $(USER_OBJ_ROOT)/serial-consoled/serial-consoled.elf
SERIAL_CONSOLED_BINARY        := $(USER_OBJ_ROOT)/serial-consoled/serial-consoled.bin
SERIAL_CONSOLED_EMBED_OBJECT  := $(KERNEL_OBJ_ROOT)/user_serial_consoled.o
SERIAL_CONSOLED_LIBSHARKIX    := $(CONFIG_BUILD_ROOT)/libsharkix/libsharkix-user.a

SERIAL_CONSOLED_CFLAGS := \
    -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
    -m64 -fno-stack-protector -fno-pic -fno-pie \
    -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
    -fno-asynchronous-unwind-tables
SERIAL_CONSOLED_ASFLAGS := -x assembler-with-cpp -ffreestanding -m64
SERIAL_CONSOLED_CPPFLAGS := -I$(INCLUDE_ROOT)

SERIAL_CONSOLED_BINARY_NAME := $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(SERIAL_CONSOLED_BINARY))
SERIAL_CONSOLED_BINARY_SYMBOL := _binary_$(subst /,_,$(subst .,_,$(subst -,_,$(SERIAL_CONSOLED_BINARY_NAME))))

$(SERIAL_CONSOLED_LIBSHARKIX): FORCE
	$(MAKE) -C $(SHARKIX_PROJECT_ROOT)/src/libsharkix \
		BUILD_OUT=$(CONFIG_BUILD_ROOT)/libsharkix \
		INCLUDE=$(INCLUDE_ROOT) \
		DEBUG=$(DEBUG) \
		$(SERIAL_CONSOLED_LIBSHARKIX)

$(SERIAL_CONSOLED_ENTRY_OBJECT): $(SERIAL_CONSOLED_ENTRY_SOURCE)
	$(MKDIR_P) $(@D)
	$(CC) $(SERIAL_CONSOLED_CPPFLAGS) $(SERIAL_CONSOLED_ASFLAGS) -c $< -o $@

$(SERIAL_CONSOLED_OBJECT): $(SERIAL_CONSOLED_SOURCE)
	$(MKDIR_P) $(@D)
	$(CC) $(SERIAL_CONSOLED_CPPFLAGS) $(SERIAL_CONSOLED_CFLAGS) -c $< -o $@

$(SERIAL_CONSOLED_ELF): $(SERIAL_CONSOLED_ENTRY_OBJECT) $(SERIAL_CONSOLED_OBJECT) $(SERIAL_CONSOLED_LINKER_SCRIPT) $(SERIAL_CONSOLED_LIBSHARKIX)
	$(MKDIR_P) $(@D)
	$(LD) -m $(USER_LINKER_FORMAT) -nostdlib -o $@ \
		$(SERIAL_CONSOLED_ENTRY_OBJECT) $(SERIAL_CONSOLED_OBJECT) $(SERIAL_CONSOLED_LIBSHARKIX) \
		-T $(SERIAL_CONSOLED_LINKER_SCRIPT)

$(SERIAL_CONSOLED_BINARY): $(SERIAL_CONSOLED_ELF)
	$(OBJCOPY) -O binary $< $@

$(SERIAL_CONSOLED_EMBED_OBJECT): $(SERIAL_CONSOLED_BINARY)
	$(MKDIR_P) $(@D)
	cd $(SHARKIX_PROJECT_ROOT) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(SERIAL_CONSOLED_BINARY_SYMBOL)_start=serial_consoled_image_start \
		--redefine-sym $(SERIAL_CONSOLED_BINARY_SYMBOL)_end=serial_consoled_image_end \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$<) \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$@)

KERNEL_C_SRCS += $(SHARKIX_PROJECT_ROOT)/src/kernel/../drivers/serial-consoled/kernel/console-serial.c 

USER_OBJECTS += $(SERIAL_CONSOLED_EMBED_OBJECT)
