DRIVER_MODULE_ROOT := $(DRIVER_SRC_ROOT)
DRIVER_SRCS :=

DRIVER_COMMON_ROOT := $(DRIVER_MODULE_ROOT)/common

DRIVER_RING3_LINKER := $(DRIVER_COMMON_ROOT)/ring3.ld

$(foreach driver,$(PLATFORM_DRIVERS),\
      $(eval $(call include-selection,$(DRIVER_SRC_ROOT)/$(driver)/module.mk)))


