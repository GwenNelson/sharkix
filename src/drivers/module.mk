DRIVER_MODULE_ROOT := $(DRIVER_SRC_ROOT)
DRIVER_SRCS :=


$(foreach driver,$(PLATFORM_DRIVERS),\
      $(eval $(call include-selection,$(DRIVER_SRC_ROOT)/$(driver)/module.mk)))


