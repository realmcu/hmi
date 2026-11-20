# ================================================================
# POSIX device abstraction layer — example build system
#
# When integrating this Makefile into your project:
#   1. Change TARGET to your target chip
#   2. Add your CC/CFLAGS/LDSCRIPT
#   3. Point PORT_DIR at your platform directory
# ================================================================

# ---------- Toolchain (modify for your platform) ----------
CROSS_COMPILE ?= arm-none-eabi-
CC       := $(CROSS_COMPILE)gcc
AR       := $(CROSS_COMPILE)ar
OBJCOPY  := $(CROSS_COMPILE)objcopy
SIZE     := $(CROSS_COMPILE)size

# ---------- Directories ----------
ROOT_DIR    := .
CORE_DIR    := $(ROOT_DIR)/core
PORT_DIR    := $(ROOT_DIR)/port/custom-rtos
EXAMPLES_DIR := $(ROOT_DIR)/examples

# ---------- Compile options ----------
CFLAGS := -Wall -Wextra -Werror -Wno-unused-parameter
CFLAGS += -Os -g0
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -I$(ROOT_DIR)/include
CFLAGS += -I$(PORT_DIR)
CFLAGS += -MMD -MP

LDFLAGS := -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/output.map

# ---------- Source files ----------
SRCS := $(wildcard $(CORE_DIR)/*.c)
SRCS += $(wildcard $(PORT_DIR)/*.c)

# ---------- Build directory ----------
BUILD_DIR := build
OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

# ---------- Targets ----------
TARGET := libposix_device.a

.PHONY: all clean examples

all: $(BUILD_DIR)/$(TARGET)

# Compile .c → .o
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Package the static library
$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(AR) rcs $@ $^
	$(SIZE) $@

# Build the example (if EXAMPLE is defined)
ifeq ($(EXAMPLE),)
else
EXAMPLE_SRC := $(EXAMPLES_DIR)/$(EXAMPLE)
EXAMPLE_ELF := $(BUILD_DIR)/$(EXAMPLE:.c=).elf

example: $(EXAMPLE_ELF)

$(EXAMPLE_ELF): $(BUILD_DIR)/$(TARGET) $(EXAMPLE_SRC)
	$(CC) $(CFLAGS) -o $@ $(EXAMPLE_SRC) $< $(LDFLAGS)
	$(SIZE) $@
endif

# ---------- Clean ----------
clean:
	rm -rf $(BUILD_DIR)

# ---------- Automatic dependencies ----------
-include $(DEPS)
