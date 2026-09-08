# Sharkix's build is one GNU Make graph. Configurations select policy;
# module.mk files describe the existing implementation in place.
SHARKIX_TOPLEVEL_MAKEFILE := $(lastword $(MAKEFILE_LIST))
include $(dir $(SHARKIX_TOPLEVEL_MAKEFILE))mk/common.mk
include $(MK_ROOT)/functions.mk

$(call require-variable,CONFIG)
CONFIG_FILE := $(CONFIG_ROOT)/$(CONFIG).mk
$(call require-file,$(CONFIG_FILE))
include $(CONFIG_FILE)

$(call require-variable,ARCH)
$(call require-variable,ARCH_BITS)
$(call require-variable,BOOT)
include $(MK_ROOT)/arch/$(ARCH)/$(ARCH_BITS).mk
$(foreach component,$(PLATFORM_COMPONENTS),$(eval $(call include-selection,$(MK_ROOT)/platform/$(component).mk)))
include $(MK_ROOT)/boot/$(BOOT).mk
include $(MK_ROOT)/profiles/$(PROFILE).mk

include $(KERNEL_SRC_ROOT)/module.mk
$(foreach module,$(KERNEL_ARCH_MODULE_ROOTS),$(eval $(call include-module,$(module))))
include $(KERNEL_SRC_ROOT)/freertos/module.mk
include $(USER_SRC_ROOT)/module.mk
include $(MK_ROOT)/libfifo.mk
include $(MK_ROOT)/rules.mk
