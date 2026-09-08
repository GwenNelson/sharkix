ifeq ($(ARCH),x86)
else
$(error platform component pc-generic requires ARCH=x86)
endif
QEMU_DISPLAY ?= -display none
QEMU_ARGS += $(QEMU_ACCEL_FLAGS) -m 512M -serial stdio $(QEMU_DISPLAY) -no-reboot
