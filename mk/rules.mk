$(call require-variable,KERNEL_LINKER_SCRIPT)
$(call require-variable,KERNEL_LINKER_FORMAT)
$(call require-variable,USER_LINKER_SCRIPT)
$(call require-variable,USER_LINKER_FORMAT)

KERNEL_OBJECTS := $(patsubst $(KERNEL_SRC_ROOT)/%.c,$(KERNEL_OBJ_ROOT)/%.o,$(KERNEL_C_SRCS)) \
 $(patsubst $(KERNEL_SRC_ROOT)/%.S,$(KERNEL_OBJ_ROOT)/%.o,$(KERNEL_ASM_SRCS))
USER_ASM_OBJECTS := $(patsubst $(USER_SRC_ROOT)/%.s,$(USER_OBJ_ROOT)/%.o,$(USER_ASM_SRCS))
USER_ELFS := $(patsubst $(USER_OBJ_ROOT)/%.o,$(USER_OBJ_ROOT)/%.elf,$(USER_ASM_OBJECTS))
KERNEL_DEPFILES := $(KERNEL_OBJECTS:.o=.d)

.PHONY: all clean iso run run-gdb run-iso verify FORCE _run-gdb
.SECONDARY: $(USER_ASM_OBJECTS) $(USER_ELFS)
.DEFAULT_GOAL := all
-include $(KERNEL_DEPFILES)

all: kernel.elf
kernel.elf: $(KERNEL_ELF)
	ln -sfn $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$<) $@

$(KERNEL_ELF): FORCE $(KERNEL_OBJECTS) $(USER_OBJECTS) $(LIBFIFO_ARTIFACT) $(KERNEL_LINKER_SCRIPT) $(INCLUDE_ROOT)/FreeRTOSConfig.h
	$(MKDIR_P) $(@D)
	$(LD) $(KERNEL_LDFLAGS) -o $@ $(KERNEL_OBJECTS) $(USER_OBJECTS) $(LIBFIFO_ARTIFACT)
FORCE:

$(KERNEL_OBJ_ROOT)/%.o: $(KERNEL_SRC_ROOT)/%.c $(INCLUDE_ROOT)/FreeRTOSConfig.h
	$(MKDIR_P) $(@D)
	$(CC) $(KERNEL_CFLAGS) -MMD -MP $(KERNEL_CPPFLAGS) -c $< -o $@

$(KERNEL_OBJ_ROOT)/%.o: $(KERNEL_SRC_ROOT)/%.S $(INCLUDE_ROOT)/FreeRTOSConfig.h
	$(MKDIR_P) $(@D)
	$(CC) $(KERNEL_ASFLAGS) -MMD -MP $(KERNEL_CPPFLAGS) -c $< -o $@

$(USER_OBJ_ROOT)/%.o: $(USER_SRC_ROOT)/%.s
	$(MKDIR_P) $(@D)
	$(CC) $(USER_ASFLAGS) -c $< -o $@
$(USER_OBJ_ROOT)/%.elf: $(USER_OBJ_ROOT)/%.o $(USER_LINKER_SCRIPT)
	$(LD) $(USER_LDFLAGS) -o $@ $<
$(USER_OBJ_ROOT)/%.bin: $(USER_OBJ_ROOT)/%.elf
	$(OBJCOPY) -O binary $< $@

define USER_EMBED_RULE
$(KERNEL_OBJ_ROOT)/user_$(1).o: $(USER_OBJ_ROOT)/$(2).bin
	$(MKDIR_P) $$(@D)
	cd $(SHARKIX_PROJECT_ROOT) && $(LD) -r -b binary -m $(USER_LINKER_FORMAT) -o $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(KERNEL_OBJ_ROOT)/user_$(1).o) $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(USER_OBJ_ROOT)/$(2).bin)
	cd $(SHARKIX_PROJECT_ROOT) && $(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	 --redefine-sym _binary_$(subst .,_,$(subst -,_,$(subst /,_,$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(USER_OBJ_ROOT)/$(2).bin))))_start=$(3)_image_start \
	 --redefine-sym _binary_$(subst .,_,$(subst -,_,$(subst /,_,$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(USER_OBJ_ROOT)/$(2).bin))))_end=$(3)_image_end $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(KERNEL_OBJ_ROOT)/user_$(1).o)
USER_OBJECTS += $(KERNEL_OBJ_ROOT)/user_$(1).o
endef
$(foreach spec,$(USER_EMBED_SPECS),$(eval $(call USER_EMBED_RULE,$(word 1,$(subst |, ,$(spec))),$(word 2,$(subst |, ,$(spec))),$(word 3,$(subst |, ,$(spec))))))

# USER_OBJECTS is populated by the embed-rule templates above; add it only
# after those templates have expanded so it is a real dependency, not merely a
# name expanded in the final link recipe.
$(KERNEL_ELF): $(USER_OBJECTS)

$(LIBFIFO_BUILD_ROOT)/fifo/%.o: $(LIBFIFO_MODULE_ROOT)/src/fifo/%.c
	$(MKDIR_P) $(@D)
	$(CC) $(LIBFIFO_CPPFLAGS) $(LIBFIFO_CFLAGS) -c $< -o $@
$(LIBFIFO_ARTIFACT): $(LIBFIFO_OBJECTS)
	$(MKDIR_P) $(@D)
	$(AR) rcs $@ $^

$(BOOTSTUB32_BUILD_ROOT)/bootstub32.o: $(BOOTSTUB32_MODULE_ROOT)/bootstub32.S
	$(MKDIR_P) $(@D)
	$(CC) $(BOOTSTUB32_ASFLAGS) -c $< -o $@
$(BOOTSTUB32_BUILD_ROOT)/bootstub32_c.o: $(BOOTSTUB32_MODULE_ROOT)/bootstub32.c
	$(MKDIR_P) $(@D)
	$(CC) $(BOOTSTUB32_CFLAGS) -c $< -o $@
$(BOOTSTUB32_ARTIFACT): $(BOOTSTUB32_OBJECTS) $(BOOTSTUB32_MODULE_ROOT)/bootstub32.ld
	$(MKDIR_P) $(@D)
	$(LD) $(BOOTSTUB32_LDFLAGS) -o $@ $(BOOTSTUB32_OBJECTS)
bootstub32/bootstub32: $(BOOTSTUB32_ARTIFACT)
	ln -sfn $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$<) $@

run: $(KERNEL_ELF) $(BOOT_ARTIFACTS)
	$(QEMU) $(QEMU_ARGS) -kernel $(BOOTSTUB32_ARTIFACT) -initrd $(KERNEL_ELF)

# Preserve the historical public target and its clean debug rebuild behavior.
run-gdb:
	$(MAKE) clean
	$(MAKE) DEBUG=1 _run-gdb
_run-gdb: $(KERNEL_ELF) $(BOOT_ARTIFACTS)
	$(QEMU) $(QEMU_ARGS) -kernel $(BOOTSTUB32_ARTIFACT) -initrd $(KERNEL_ELF) -S -s

$(ISO_IMAGE): $(KERNEL_ELF) $(SHARKIX_PROJECT_ROOT)/grub.cfg
	$(MKDIR_P) $(ISO_ROOT)/boot/grub
	cp $(KERNEL_ELF) $(ISO_ROOT)/boot/kernel.elf
	cp $(SHARKIX_PROJECT_ROOT)/grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(ISO_ROOT) >/dev/null
iso: $(ISO_IMAGE)
	ln -sfn $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$<) sharkix.iso
run-iso: $(ISO_IMAGE)
	$(QEMU) $(QEMU_ARGS) -cdrom $(ISO_IMAGE)
verify: $(KERNEL_ELF)
	readelf -h $(KERNEL_ELF)
	readelf -l $(KERNEL_ELF)
	grub-file --is-x86-multiboot $(KERNEL_ELF)

clean:
	rm -rf $(BUILD_ROOT)
	rm -f kernel.elf sharkix.iso bootstub32/bootstub32
