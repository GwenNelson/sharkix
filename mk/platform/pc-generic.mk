ifeq ($(ARCH),x86)
else
$(error platform component pc-generic requires ARCH=x86)
endif

QEMU_ARGS += $(QEMU_ACCEL_FLAGS) -m 512M -serial stdio -display none -no-reboot
