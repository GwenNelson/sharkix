ifeq ($(BOOT),multiboot1)
else
$(error multiboot1 selection loaded for BOOT=$(BOOT))
endif

$(call include-module,$(BOOTSTUB32_ROOT))
BOOT_ARTIFACTS += $(BOOTSTUB32_ARTIFACT)
