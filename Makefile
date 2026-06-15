# ================================================================
# POSIX 设备抽象层 — 示例构建系统
#
# 将此 Makefile 集成到你的项目中时：
#   1. 修改 TARGET 为你的目标芯片
#   2. 添加你的 CC/CFLAGS/LDSCRIPT
#   3. 将 PORT_DIR 指向你的平台目录
# ================================================================

# ---------- 工具链（根据实际平台修改） ----------
CROSS_COMPILE ?= arm-none-eabi-
CC       := $(CROSS_COMPILE)gcc
AR       := $(CROSS_COMPILE)ar
OBJCOPY  := $(CROSS_COMPILE)objcopy
SIZE     := $(CROSS_COMPILE)size

# ---------- 目录 ----------
ROOT_DIR    := .
CORE_DIR    := $(ROOT_DIR)/core
PORT_DIR    := $(ROOT_DIR)/port/custom-rtos
EXAMPLES_DIR := $(ROOT_DIR)/examples

# ---------- 编译选项 ----------
CFLAGS := -Wall -Wextra -Werror -Wno-unused-parameter
CFLAGS += -Os -g0
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -I$(ROOT_DIR)/include
CFLAGS += -I$(PORT_DIR)
CFLAGS += -MMD -MP

LDFLAGS := -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/output.map

# ---------- 源文件 ----------
SRCS := $(wildcard $(CORE_DIR)/*.c)
SRCS += $(wildcard $(PORT_DIR)/*.c)

# ---------- 构建目录 ----------
BUILD_DIR := build
OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

# ---------- 目标 ----------
TARGET := libposix_device.a

.PHONY: all clean examples

all: $(BUILD_DIR)/$(TARGET)

# 编译 .c → .o
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# 打包静态库
$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(AR) rcs $@ $^
	$(SIZE) $@

# 构建示例（如果定义了 EXAMPLE）
ifeq ($(EXAMPLE),)
else
EXAMPLE_SRC := $(EXAMPLES_DIR)/$(EXAMPLE)
EXAMPLE_ELF := $(BUILD_DIR)/$(EXAMPLE:.c=).elf

example: $(EXAMPLE_ELF)

$(EXAMPLE_ELF): $(BUILD_DIR)/$(TARGET) $(EXAMPLE_SRC)
	$(CC) $(CFLAGS) -o $@ $(EXAMPLE_SRC) $< $(LDFLAGS)
	$(SIZE) $@
endif

# ---------- 清理 ----------
clean:
	rm -rf $(BUILD_DIR)

# ---------- 自动依赖 ----------
-include $(DEPS)
