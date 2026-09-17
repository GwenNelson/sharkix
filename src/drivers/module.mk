DRIVER_MODULE_ROOT := $(DRIVER_SRC_ROOT)
DRIVER_SRCS :=

DRIVER_COMMON_ROOT := $(DRIVER_MODULE_ROOT)/common

DRIVER_RING3_LINKER := $(DRIVER_COMMON_ROOT)/driver-ring3.ld
DRIVER_RING3_ENTRY  := $(DRIVER_COMMON_ROOT)/driver-ring3-entry.S

DRIVER_RING3_ENTRY_OBJECT := $(USER_OBJ_ROOT)/driver-ring3-entry.o
DRIVER_RING3_ENTRY_SOURCE := $(DRIVER_RING3_ENTRY)


DRIVER_RING3_CFLAGS := \
    -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
    -m64 -fno-stack-protector -fno-pic -fno-pie \
    -mno-red-zone -mno-sse -mno-mmx -mno-80387 \
    -fno-asynchronous-unwind-tables
DRIVER_RING3_ASFLAGS := -x assembler-with-cpp -ffreestanding -m64
DRIVER_RING3_CPPFLAGS := -I$(INCLUDE_ROOT)

$(DRIVER_RING3_ENTRY_OBJECT): $(DRIVER_RING3_ENTRY_SOURCE)
	$(MKDIR_P) $(@D)
	$(CC) $(DRIVER_RING3_CPPFLAGS) $(DRIVER_RING3_ASFLAGS) -c $< -o $@


$(foreach driver,$(PLATFORM_DRIVERS),\
      $(eval $(call include-selection,$(DRIVER_SRC_ROOT)/$(driver)/module.mk)))


