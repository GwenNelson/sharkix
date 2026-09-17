DRIVER_MODULE_ROOT := $(DRIVER_SRC_ROOT)
DRIVER_COMMON_ROOT := $(DRIVER_MODULE_ROOT)/common

DRIVER_RING3_LINKER := $(DRIVER_COMMON_ROOT)/driver-ring3.ld
DRIVER_RING3_ENTRY := $(DRIVER_COMMON_ROOT)/driver-ring3-entry.S
DRIVER_RING3_ENTRY_OBJECT := $(USER_OBJ_ROOT)/driver-ring3-entry.o
DRIVER_RING3_LIB := $(CONFIG_BUILD_ROOT)/libsharkix/libsharkix-user.a

DRIVER_RING3_CFLAGS := \
    -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
    -m64 -fno-stack-protector -fno-pic -fno-pie \
    -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
    -fno-asynchronous-unwind-tables
DRIVER_RING3_ASFLAGS := -x assembler-with-cpp -ffreestanding -m64
DRIVER_RING3_CPPFLAGS := -I$(INCLUDE_ROOT)
DRIVER_DEPFILES :=

$(DRIVER_RING3_LIB): FORCE
	$(MAKE) -C $(SHARKIX_PROJECT_ROOT)/src/libsharkix \
		BUILD_OUT=$(CONFIG_BUILD_ROOT)/libsharkix \
		INCLUDE=$(INCLUDE_ROOT) DEBUG=$(DEBUG) $(DRIVER_RING3_LIB)

$(DRIVER_RING3_ENTRY_OBJECT): $(DRIVER_RING3_ENTRY)
	$(MKDIR_P) $(@D)
	$(CC) $(DRIVER_RING3_CPPFLAGS) $(DRIVER_RING3_ASFLAGS) -MMD -MP -c $< -o $@

DRIVER_DEPFILES += $(DRIVER_RING3_ENTRY_OBJECT:.o=.d)

define ring3-driver
$(eval DRIVER_$(1)_KERNEL_SRCS := $(wildcard $(DRIVER_MODULE_ROOT)/$(1)/kernel/*.c))
$(eval DRIVER_$(1)_USER_SRCS := $(wildcard $(DRIVER_MODULE_ROOT)/$(1)/user/*.c))
$(eval DRIVER_$(1)_USER_OBJECTS := $(patsubst $(DRIVER_MODULE_ROOT)/$(1)/user/%.c,$(USER_OBJ_ROOT)/$(1)/user/%.o,$(DRIVER_$(1)_USER_SRCS)))
$(eval DRIVER_$(1)_ELF := $(USER_OBJ_ROOT)/$(1)/$(1).elf)
$(eval DRIVER_$(1)_BINARY := $(USER_OBJ_ROOT)/$(1)/$(1).bin)
$(eval DRIVER_$(1)_EMBED := $(KERNEL_OBJ_ROOT)/$(1)/ring3-embed.o)
$(eval DRIVER_$(1)_SYMBOL := $(subst -,_,$(1)))
$(eval DRIVER_$(1)_BINARY_NAME := $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(DRIVER_$(1)_BINARY)))
$(eval DRIVER_$(1)_BINARY_SYMBOL := _binary_$(subst .,_,$(subst -,_,$(subst /,_,$(DRIVER_$(1)_BINARY_NAME)))))
$(eval KERNEL_C_SRCS += $(patsubst $(DRIVER_MODULE_ROOT)/$(1)/kernel/%.c,$(KERNEL_SRC_ROOT)/../drivers/$(1)/kernel/%.c,$(DRIVER_$(1)_KERNEL_SRCS)))
$(eval USER_OBJECTS += $$(DRIVER_$(1)_EMBED))
$(eval DRIVER_DEPFILES += $$(DRIVER_$(1)_USER_OBJECTS:.o=.d))

$(DRIVER_$(1)_USER_OBJECTS): $(USER_OBJ_ROOT)/$(1)/user/%.o: $(DRIVER_MODULE_ROOT)/$(1)/user/%.c
	$$(MKDIR_P) $$(@D)
	$$(CC) $$(DRIVER_RING3_CPPFLAGS) $$(DRIVER_RING3_CFLAGS) -MMD -MP -c $$< -o $$@

$(DRIVER_$(1)_ELF): $$(DRIVER_RING3_ENTRY_OBJECT) $$(DRIVER_$(1)_USER_OBJECTS) $$(DRIVER_RING3_LINKER) $$(DRIVER_RING3_LIB)
	$$(MKDIR_P) $$(@D)
	$$(LD) -m $$(USER_LINKER_FORMAT) -nostdlib -o $$@ $$(DRIVER_RING3_ENTRY_OBJECT) $$(DRIVER_$(1)_USER_OBJECTS) $$(DRIVER_RING3_LIB) -T $$(DRIVER_RING3_LINKER)

$(DRIVER_$(1)_BINARY): $(DRIVER_$(1)_ELF)
	$$(OBJCOPY) -O binary $$< $$@

$(DRIVER_$(1)_EMBED): $(DRIVER_$(1)_BINARY)
	$$(MKDIR_P) $$(@D)
	cd $$(SHARKIX_PROJECT_ROOT) && $$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $$(DRIVER_$(1)_BINARY_SYMBOL)_start=$$(DRIVER_$(1)_SYMBOL)_image_start \
		--redefine-sym $$(DRIVER_$(1)_BINARY_SYMBOL)_end=$$(DRIVER_$(1)_SYMBOL)_image_end \
		$$(patsubst $$(SHARKIX_PROJECT_ROOT)/%,%,$$<) $$(patsubst $$(SHARKIX_PROJECT_ROOT)/%,%,$$@)
endef

$(foreach driver,$(PLATFORM_DRIVERS),$(eval $(call include-selection,$(DRIVER_MODULE_ROOT)/$(driver)/module.mk)))
