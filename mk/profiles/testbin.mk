PROFILE_SOURCE := $(KERNEL_SRC_ROOT)/startup/testbin.c

TESTBIN_SOURCE := $(USER_SRC_ROOT)/testbin/main.c
TESTBIN_LINKER_SCRIPT := $(USER_SRC_ROOT)/testbin/testbin.ld
TESTBIN_OBJECT := $(USER_OBJ_ROOT)/testbin/main.o
TESTBIN_ELF := $(USER_OBJ_ROOT)/testbin/testbin.elf
TESTBIN_BINARY := $(USER_OBJ_ROOT)/testbin/testbin.bin
TESTBIN_EMBED_OBJECT := $(KERNEL_OBJ_ROOT)/user_testbin.o
TESTBIN_LIBSHARKIX := $(CONFIG_BUILD_ROOT)/libsharkix/libsharkix-user.a

TESTBIN_CFLAGS := \
    -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
    -m64 -fno-stack-protector -fno-pic -fno-pie \
    -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
    -fno-asynchronous-unwind-tables
TESTBIN_CPPFLAGS := -I$(INCLUDE_ROOT)

USER_LINKER_SCRIPT := $(TESTBIN_LINKER_SCRIPT)

TESTBIN_BINARY_NAME := $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(TESTBIN_BINARY))
TESTBIN_BINARY_SYMBOL := _binary_$(subst /,_,$(subst .,_,$(subst -,_,$(TESTBIN_BINARY_NAME))))

$(TESTBIN_LIBSHARKIX): FORCE
	$(MAKE) -C $(SHARKIX_PROJECT_ROOT)/src/libsharkix \
		BUILD_OUT=$(CONFIG_BUILD_ROOT)/libsharkix \
		INCLUDE=$(INCLUDE_ROOT) \
		DEBUG=$(DEBUG) \
		$(TESTBIN_LIBSHARKIX)

$(TESTBIN_OBJECT): $(TESTBIN_SOURCE)
	$(MKDIR_P) $(@D)
	$(CC) $(TESTBIN_CPPFLAGS) $(TESTBIN_CFLAGS) -c $< -o $@

$(TESTBIN_ELF): $(TESTBIN_OBJECT) $(TESTBIN_LINKER_SCRIPT) $(TESTBIN_LIBSHARKIX)
	$(MKDIR_P) $(@D)
	$(LD) $(USER_LDFLAGS) -o $@ $(TESTBIN_OBJECT) $(TESTBIN_LIBSHARKIX)

$(TESTBIN_BINARY): $(TESTBIN_ELF)
	$(OBJCOPY) -O binary $< $@

$(TESTBIN_EMBED_OBJECT): $(TESTBIN_BINARY)
	$(MKDIR_P) $(@D)
	cd $(SHARKIX_PROJECT_ROOT) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(TESTBIN_BINARY_SYMBOL)_start=testbin_image_start \
		--redefine-sym $(TESTBIN_BINARY_SYMBOL)_end=testbin_image_end \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$<) \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$@)

USER_OBJECTS += $(TESTBIN_EMBED_OBJECT)
