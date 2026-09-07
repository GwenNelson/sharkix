ifeq ($(ARCH),x86)
else
$(error x86 selection loaded for ARCH=$(ARCH))
endif
