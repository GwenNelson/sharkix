PROFILE_SOURCE := $(KERNEL_SRC_ROOT)/profiles/kthread_test.c

KTHREAD_TEST_DIR := $(USER_SRC_ROOT)/kthread_test
KTHREAD_TEST_ENTRY_SOURCE := $(KTHREAD_TEST_DIR)/entry.S
KTHREAD_TEST_SOURCE := $(KTHREAD_TEST_DIR)/main.c
KTHREAD_TEST_LINKER_SCRIPT := $(KTHREAD_TEST_DIR)/kthread_test.ld
KTHREAD_TEST_ENTRY_OBJECT := $(USER_OBJ_ROOT)/kthread_test/entry.o
KTHREAD_TEST_OBJECT := $(USER_OBJ_ROOT)/kthread_test/main.o
KTHREAD_TEST_ELF := $(USER_OBJ_ROOT)/kthread_test/kthread_test.elf
KTHREAD_TEST_BINARY := $(USER_OBJ_ROOT)/kthread_test/kthread_test.bin
KTHREAD_TEST_EMBED_OBJECT := $(KERNEL_OBJ_ROOT)/user_kthread_test.o
KTHREAD_TEST_LIBSHARKIX := $(CONFIG_BUILD_ROOT)/libsharkix/libsharkix-user.a

KTHREAD_TEST_CFLAGS := \
    -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
    -m64 -fno-stack-protector -fno-pic -fno-pie \
    -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
    -fno-asynchronous-unwind-tables
KTHREAD_TEST_ASFLAGS := -x assembler-with-cpp -ffreestanding -m64
KTHREAD_TEST_CPPFLAGS := -I$(INCLUDE_ROOT)

USER_LINKER_SCRIPT := $(KTHREAD_TEST_LINKER_SCRIPT)

KTHREAD_TEST_BINARY_NAME := $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(KTHREAD_TEST_BINARY))
KTHREAD_TEST_BINARY_SYMBOL := _binary_$(subst /,_,$(subst .,_,$(subst -,_,$(KTHREAD_TEST_BINARY_NAME))))

$(KTHREAD_TEST_ENTRY_OBJECT): $(KTHREAD_TEST_ENTRY_SOURCE)
	$(MKDIR_P) $(@D)
	$(CC) $(KTHREAD_TEST_CPPFLAGS) $(KTHREAD_TEST_ASFLAGS) -c $< -o $@

$(KTHREAD_TEST_OBJECT): $(KTHREAD_TEST_SOURCE)
	$(MKDIR_P) $(@D)
	$(CC) $(KTHREAD_TEST_CPPFLAGS) $(KTHREAD_TEST_CFLAGS) -c $< -o $@

$(KTHREAD_TEST_ELF): $(KTHREAD_TEST_ENTRY_OBJECT) $(KTHREAD_TEST_OBJECT) $(KTHREAD_TEST_LINKER_SCRIPT) $(KTHREAD_TEST_LIBSHARKIX)
	$(MKDIR_P) $(@D)
	$(LD) -m $(USER_LINKER_FORMAT) -nostdlib -o $@ \
		$(KTHREAD_TEST_ENTRY_OBJECT) $(KTHREAD_TEST_OBJECT) $(KTHREAD_TEST_LIBSHARKIX) \
		-T $(KTHREAD_TEST_LINKER_SCRIPT)

$(KTHREAD_TEST_BINARY): $(KTHREAD_TEST_ELF)
	$(OBJCOPY) -O binary $< $@

$(KTHREAD_TEST_EMBED_OBJECT): $(KTHREAD_TEST_BINARY)
	$(MKDIR_P) $(@D)
	cd $(SHARKIX_PROJECT_ROOT) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(KTHREAD_TEST_BINARY_SYMBOL)_start=kthread_test_image_start \
		--redefine-sym $(KTHREAD_TEST_BINARY_SYMBOL)_end=kthread_test_image_end \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$<) \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$@)

USER_OBJECTS += $(KTHREAD_TEST_EMBED_OBJECT)
