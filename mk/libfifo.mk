LIBFIFO_MODULE_ROOT := $(EXTERNAL_ROOT)/libfifo
LIBFIFO_BUILD_ROOT = $(CONFIG_BUILD_ROOT)/libfifo
LIBFIFO_ARTIFACT = $(LIBFIFO_BUILD_ROOT)/libfifo.a
LIBFIFO_C_SRCS := $(wildcard $(LIBFIFO_MODULE_ROOT)/src/fifo/*.c)
LIBFIFO_OBJECTS := $(patsubst $(LIBFIFO_MODULE_ROOT)/src/fifo/%.c,$(LIBFIFO_BUILD_ROOT)/fifo/%.o,$(LIBFIFO_C_SRCS))

# The root Makefile historically overrides libfifo's CFLAGS wholesale. Keep
# this exact effective sequence; only -Iinclude becomes an absolute path.
LIBFIFO_CPPFLAGS = -I$(LIBFIFO_MODULE_ROOT)/include
LIBFIFO_CFLAGS = $(KERNEL_CFLAGS)
