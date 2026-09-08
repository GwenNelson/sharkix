ifeq ($(BOOT),multiboot1)
else
$(error multiboot1 selection loaded for BOOT=$(BOOT))
endif

ifeq ($(ARCH),x86)
else
$(error multiboot1 currently requires ARCH=x86, got ARCH=$(ARCH))
endif

ifeq ($(ARCH_BITS),64)
include $(MK_ROOT)/boot/bootstub32.mk
MULTIBOOT1_KERNEL_ARGS = -kernel $(BOOTSTUB32_ARTIFACT) -initrd $(KERNEL_ELF)
else
MULTIBOOT1_KERNEL_ARGS = -kernel $(KERNEL_ELF)
endif

RUN_COMMAND = $(QEMU) $(QEMU_ARGS) $(MULTIBOOT1_KERNEL_ARGS)
RUN_GDB_COMMAND = $(QEMU) $(QEMU_ARGS) $(MULTIBOOT1_KERNEL_ARGS) -S -s
RUN_ISO_COMMAND = $(QEMU) $(QEMU_ARGS) -cdrom $(ISO_IMAGE)
VERIFY_COMMAND = readelf -h $(KERNEL_ELF); readelf -l $(KERNEL_ELF); grub-file --is-x86-multiboot $(KERNEL_ELF)
ISO_KERNEL_PATH = $(ISO_ROOT)/boot/kernel.elf
ISO_BOOT_CONFIG_SOURCE = $(SHARKIX_PROJECT_ROOT)/grub.cfg
ISO_BOOT_CONFIG_PATH = $(ISO_ROOT)/boot/grub/grub.cfg
ISO_CREATE_COMMAND = $(GRUB_MKRESCUE) -o $(ISO_IMAGE) $(ISO_ROOT) >/dev/null
ISO_LINK_PATH = sharkix.iso

CLEAN_COMMAND += && rm -f sharkix.iso
