PROFILE_SOURCE := $(KERNEL_SRC_ROOT)/startup/testelf.c

TESTELF_DIR := $(USER_SRC_ROOT)/testelf
TESTELF_LOADER_ENTRY_OBJECT := $(USER_OBJ_ROOT)/testelf/entry.o
TESTELF_LOADER_OBJECT := $(USER_OBJ_ROOT)/testelf/loader.o
TESTELF_LOADER_ELF := $(USER_OBJ_ROOT)/testelf/loader.elf
TESTELF_LOADER_BINARY := $(USER_OBJ_ROOT)/testelf/loader.bin
TESTELF_LOADER_EMBED_OBJECT := $(KERNEL_OBJ_ROOT)/testelf_loader.o
TESTELF_PAYLOAD_OBJECT := $(USER_OBJ_ROOT)/testelf/payload.o
TESTELF_PAYLOAD_ELF := $(USER_OBJ_ROOT)/testelf/payload.elf
TESTELF_PAYLOAD_EMBED_OBJECT := $(KERNEL_OBJ_ROOT)/testelf_payload.o
TESTELF_LIBSHARKIX := $(CONFIG_BUILD_ROOT)/libsharkix/libsharkix-user.a

TESTELF_CFLAGS := \
    -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
    -m64 -fno-stack-protector -fno-pic -fno-pie \
    -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
    -fno-asynchronous-unwind-tables
TESTELF_ASFLAGS := -x assembler-with-cpp -ffreestanding -m64
TESTELF_CPPFLAGS := -I$(INCLUDE_ROOT)

USER_LINKER_SCRIPT := $(TESTELF_DIR)/loader.ld

TESTELF_LOADER_BINARY_NAME := $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(TESTELF_LOADER_BINARY))
TESTELF_LOADER_BINARY_SYMBOL := _binary_$(subst /,_,$(subst .,_,$(subst -,_,$(TESTELF_LOADER_BINARY_NAME))))
TESTELF_PAYLOAD_ELF_NAME := $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(TESTELF_PAYLOAD_ELF))
TESTELF_PAYLOAD_ELF_SYMBOL := _binary_$(subst /,_,$(subst .,_,$(subst -,_,$(TESTELF_PAYLOAD_ELF_NAME))))

$(TESTELF_LOADER_ENTRY_OBJECT): $(TESTELF_DIR)/entry.S
	$(MKDIR_P) $(@D)
	$(CC) $(TESTELF_CPPFLAGS) $(TESTELF_ASFLAGS) -c $< -o $@

$(TESTELF_LOADER_OBJECT): $(TESTELF_DIR)/loader.c
	$(MKDIR_P) $(@D)
	$(CC) $(TESTELF_CPPFLAGS) $(TESTELF_CFLAGS) -c $< -o $@

$(TESTELF_LOADER_ELF): $(TESTELF_LOADER_ENTRY_OBJECT) $(TESTELF_LOADER_OBJECT) $(TESTELF_DIR)/loader.ld $(TESTELF_LIBSHARKIX)
	$(MKDIR_P) $(@D)
	$(LD) -m $(USER_LINKER_FORMAT) -nostdlib -o $@ \
		$(TESTELF_LOADER_ENTRY_OBJECT) $(TESTELF_LOADER_OBJECT) $(TESTELF_LIBSHARKIX) \
		-T $(TESTELF_DIR)/loader.ld

$(TESTELF_LOADER_BINARY): $(TESTELF_LOADER_ELF)
	$(OBJCOPY) -O binary $< $@

$(TESTELF_LOADER_EMBED_OBJECT): $(TESTELF_LOADER_BINARY)
	$(MKDIR_P) $(@D)
	cd $(SHARKIX_PROJECT_ROOT) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(TESTELF_LOADER_BINARY_SYMBOL)_start=testelf_loader_image_start \
		--redefine-sym $(TESTELF_LOADER_BINARY_SYMBOL)_end=testelf_loader_image_end \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$<) \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$@)

$(TESTELF_PAYLOAD_OBJECT): $(TESTELF_DIR)/payload.c
	$(MKDIR_P) $(@D)
	$(CC) $(TESTELF_CPPFLAGS) $(TESTELF_CFLAGS) -c $< -o $@

$(TESTELF_PAYLOAD_ELF): $(TESTELF_PAYLOAD_OBJECT) $(TESTELF_LIBSHARKIX)
	$(MKDIR_P) $(@D)
	$(LD) -m $(USER_LINKER_FORMAT) -nostdlib -z max-page-size=0x1000 \
		--build-id=none -e _start -o $@ $< $(TESTELF_LIBSHARKIX)

$(TESTELF_PAYLOAD_EMBED_OBJECT): $(TESTELF_PAYLOAD_ELF)
	$(MKDIR_P) $(@D)
	cd $(SHARKIX_PROJECT_ROOT) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(TESTELF_PAYLOAD_ELF_SYMBOL)_start=testelf_payload_image_start \
		--redefine-sym $(TESTELF_PAYLOAD_ELF_SYMBOL)_end=testelf_payload_image_end \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$<) \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$@)

USER_OBJECTS += $(TESTELF_LOADER_EMBED_OBJECT) $(TESTELF_PAYLOAD_EMBED_OBJECT)
