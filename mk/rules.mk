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

run: $(KERNEL_ELF) $(BOOT_ARTIFACTS)
	$(if $(strip $(RUN_COMMAND)),$(RUN_COMMAND),$(error run is unsupported by the selected architecture and platform))

# Preserve the historical public target and its clean debug rebuild behavior.
run-gdb:
	$(MAKE) clean
	$(MAKE) DEBUG=1 _run-gdb
_run-gdb: $(KERNEL_ELF) $(BOOT_ARTIFACTS)
	$(if $(strip $(RUN_GDB_COMMAND)),$(RUN_GDB_COMMAND),$(error run-gdb is unsupported by the selected architecture and platform))

iso: $(KERNEL_ELF)
	$(if $(strip $(ISO_CREATE_COMMAND)),,$(error iso is unsupported by the selected architecture and platform))
	$(MKDIR_P) $(dir $(ISO_KERNEL_PATH))
	cp $(KERNEL_ELF) $(ISO_KERNEL_PATH)
	$(MKDIR_P) $(dir $(ISO_BOOT_CONFIG_PATH))
	cp $(ISO_BOOT_CONFIG_SOURCE) $(ISO_BOOT_CONFIG_PATH)
	$(ISO_CREATE_COMMAND)
	ln -sfn $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(ISO_IMAGE)) $(ISO_LINK_PATH)
run-iso: iso
	$(if $(strip $(RUN_ISO_COMMAND)),$(RUN_ISO_COMMAND),$(error run-iso is unsupported by the selected architecture and platform))
verify: $(KERNEL_ELF)
	$(if $(strip $(VERIFY_COMMAND)),$(VERIFY_COMMAND),$(error verify is unsupported by the selected architecture and platform))

clean:
	rm -rf $(BUILD_ROOT)
	rm -f kernel.elf
	$(CLEAN_COMMAND)
