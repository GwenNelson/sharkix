PROFILE_SOURCE := $(KERNEL_SRC_ROOT)/startup/vmuser.c

VMUSER_A_SOURCE := $(USER_SRC_ROOT)/vmuser_a.s
VMUSER_B_SOURCE := $(USER_SRC_ROOT)/vmuser_b.s
VMUSER_A_OBJECT := $(USER_OBJ_ROOT)/vmuser_a.o
VMUSER_B_OBJECT := $(USER_OBJ_ROOT)/vmuser_b.o
VMUSER_A_ELF := $(USER_OBJ_ROOT)/vmuser_a.elf
VMUSER_B_ELF := $(USER_OBJ_ROOT)/vmuser_b.elf
VMUSER_A_BINARY := $(USER_OBJ_ROOT)/vmuser_a.bin
VMUSER_B_BINARY := $(USER_OBJ_ROOT)/vmuser_b.bin
VMUSER_A_EMBED_OBJECT := $(KERNEL_OBJ_ROOT)/user_vmuser_a.o
VMUSER_B_EMBED_OBJECT := $(KERNEL_OBJ_ROOT)/user_vmuser_b.o

VMUSER_A_BINARY_NAME := $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(VMUSER_A_BINARY))
VMUSER_B_BINARY_NAME := $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$(VMUSER_B_BINARY))
VMUSER_A_BINARY_SYMBOL := _binary_$(subst /,_,$(subst .,_,$(subst -,_,$(VMUSER_A_BINARY_NAME))))
VMUSER_B_BINARY_SYMBOL := _binary_$(subst /,_,$(subst .,_,$(subst -,_,$(VMUSER_B_BINARY_NAME))))

$(VMUSER_A_OBJECT): $(VMUSER_A_SOURCE)
	$(MKDIR_P) $(@D)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(VMUSER_B_OBJECT): $(VMUSER_B_SOURCE)
	$(MKDIR_P) $(@D)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(VMUSER_A_ELF): $(VMUSER_A_OBJECT) $(USER_LINKER_SCRIPT)
	$(MKDIR_P) $(@D)
	$(LD) $(USER_LDFLAGS) -o $@ $<

$(VMUSER_B_ELF): $(VMUSER_B_OBJECT) $(USER_LINKER_SCRIPT)
	$(MKDIR_P) $(@D)
	$(LD) $(USER_LDFLAGS) -o $@ $<

$(VMUSER_A_BINARY): $(VMUSER_A_ELF)
	$(OBJCOPY) -O binary $< $@

$(VMUSER_B_BINARY): $(VMUSER_B_ELF)
	$(OBJCOPY) -O binary $< $@

$(VMUSER_A_EMBED_OBJECT): $(VMUSER_A_BINARY)
	$(MKDIR_P) $(@D)
	cd $(SHARKIX_PROJECT_ROOT) && $(LD) -r -b binary -m $(USER_LINKER_FORMAT) -o $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$@) $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$<)
	cd $(SHARKIX_PROJECT_ROOT) && $(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(VMUSER_A_BINARY_SYMBOL)_start=vmuser_a_image_start \
		--redefine-sym $(VMUSER_A_BINARY_SYMBOL)_end=vmuser_a_image_end \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$@)

$(VMUSER_B_EMBED_OBJECT): $(VMUSER_B_BINARY)
	$(MKDIR_P) $(@D)
	cd $(SHARKIX_PROJECT_ROOT) && $(LD) -r -b binary -m $(USER_LINKER_FORMAT) -o $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$@) $(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$<)
	cd $(SHARKIX_PROJECT_ROOT) && $(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(VMUSER_B_BINARY_SYMBOL)_start=vmuser_b_image_start \
		--redefine-sym $(VMUSER_B_BINARY_SYMBOL)_end=vmuser_b_image_end \
		$(patsubst $(SHARKIX_PROJECT_ROOT)/%,%,$@)

USER_OBJECTS += $(VMUSER_A_EMBED_OBJECT) $(VMUSER_B_EMBED_OBJECT)
