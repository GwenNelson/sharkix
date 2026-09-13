PROFILE_SOURCE := $(KERNEL_SRC_ROOT)/startup/testbin_caps.c

TESTBIN_CAPS_DIR := $(USER_SRC_ROOT)/testbin_caps
TESTBIN_CAPS_ENTRY_SOURCE := $(TESTBIN_CAPS_DIR)/entry.S
TESTBIN_CAPS_SOURCE := $(TESTBIN_CAPS_DIR)/main.c
TESTBIN_CAPS_LINKER_SCRIPT := $(TESTBIN_CAPS_DIR)/testbin_caps.ld
TESTBIN_CAPS_ENTRY_OBJECT := $(USER_OBJ_ROOT)/testbin_caps/entry.o
TESTBIN_CAPS_OBJECT := $(USER_OBJ_ROOT)/testbin_caps/main.o
TESTBIN_CAPS_ELF := $(USER_OBJ_ROOT)/testbin_caps/testbin_caps.elf
TESTBIN_CAPS_BINARY := $(USER_OBJ_ROOT)/testbin_caps/testbin_caps.bin
TESTBIN_CAPS_EMBED_OBJECT := $(KERNEL_OBJ_ROOT)/user_testbin_caps.o
TESTBIN_CAPS_LIBSHARKIX := $(CONFIG_BUILD_ROOT)/libsharkix/libsharkix-user.a

TESTBIN_CAPS_CFLAGS := \
    -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
    -m64 -fno-stack-protector -fno-pic -fno-pie \
    -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
    -fno-asynchronous-unwind-tables
TESTBIN_CAPS_ASFLAGS := -x assembler-with-cpp -ffreestanding -m64
TESTBIN_CAPS_CPPFLAGS := -I$(INCLUDE_ROOT)

USER_LINKER_SCRIPT := $(TESTBIN_CAPS_LINKER_SCRIPT)

TESTBIN_CAPS_BINARY_NAME := $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(TESTBIN_CAPS_BINARY))
TESTBIN_CAPS_BINARY_SYMBOL := _binary_$(subst /,_,$(subst .,_,$(subst -,_,$(TESTBIN_CAPS_BINARY_NAME))))

$(TESTBIN_CAPS_LIBSHARKIX): FORCE
	$(MAKE) -C $(SHARKIX_PROJECT_ROOT)/src/libsharkix \
		BUILD_OUT=$(CONFIG_BUILD_ROOT)/libsharkix \
		INCLUDE=$(INCLUDE_ROOT) \
		DEBUG=$(DEBUG) \
		$(TESTBIN_CAPS_LIBSHARKIX)

$(TESTBIN_CAPS_ENTRY_OBJECT): $(TESTBIN_CAPS_ENTRY_SOURCE)
	$(MKDIR_P) $(@D)
	$(CC) $(TESTBIN_CAPS_CPPFLAGS) $(TESTBIN_CAPS_ASFLAGS) -c $< -o $@

$(TESTBIN_CAPS_OBJECT): $(TESTBIN_CAPS_SOURCE)
	$(MKDIR_P) $(@D)
	$(CC) $(TESTBIN_CAPS_CPPFLAGS) $(TESTBIN_CAPS_CFLAGS) -c $< -o $@

$(TESTBIN_CAPS_ELF): $(TESTBIN_CAPS_ENTRY_OBJECT) $(TESTBIN_CAPS_OBJECT) $(TESTBIN_CAPS_LINKER_SCRIPT) $(TESTBIN_CAPS_LIBSHARKIX)
	$(MKDIR_P) $(@D)
	$(LD) -m $(USER_LINKER_FORMAT) -T $(TESTBIN_CAPS_LINKER_SCRIPT) -nostdlib \
		-o $@ $(TESTBIN_CAPS_ENTRY_OBJECT) $(TESTBIN_CAPS_OBJECT) $(TESTBIN_CAPS_LIBSHARKIX)

$(TESTBIN_CAPS_BINARY): $(TESTBIN_CAPS_ELF)
	$(OBJCOPY) -O binary $< $@

$(TESTBIN_CAPS_EMBED_OBJECT): $(TESTBIN_CAPS_BINARY)
	$(MKDIR_P) $(@D)
	cd $(SHARKIX_PROJECT_ROOT) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(TESTBIN_CAPS_BINARY_SYMBOL)_start=testbin_caps_image_start \
		--redefine-sym $(TESTBIN_CAPS_BINARY_SYMBOL)_end=testbin_caps_image_end \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$<) \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$@)

USER_OBJECTS += $(TESTBIN_CAPS_EMBED_OBJECT)
