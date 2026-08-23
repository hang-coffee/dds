# DDS 项目 Makefile
# 使用方式：make       - 编译并链接
#          make run    - 编译并运行
#          make clean  - 清理

# 工具路径
DCC      := ../dcc/dcc
DLINKER  := ../dlinker/dlinker
SIM      := ../doctor-sim/build/bin/doctor-sim

# 源文件目录
SRC_DIR  := src
BUILD_DIR := build

# 源文件（自动查找 src/*.c）
C_SOURCES := $(wildcard $(SRC_DIR)/*.c)
C_OBJS    := $(C_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# 启动文件（来自 dcc 的 crt）
CRT_O    := ../dcc/bootable_crt.o

# 编译选项
CFLAGS   := -m elf -ffreestanding

# 目标名称
TARGET   := test

# 默认目标：编译并链接
all: $(TARGET)

# 编译每个 .c 文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(DCC) $(CFLAGS) $< -o $@

# 链接
$(TARGET): $(CRT_O) $(C_OBJS)
	$(DLINKER) $^ -o $@

# 创建 build 目录
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 运行（编译 + 模拟）
run: $(TARGET)
	$(SIM) -f code $(TARGET)_code.bin data $(TARGET)_data.bin

# 清理
clean:
	rm -rf $(BUILD_DIR) $(TARGET)_code.bin $(TARGET)_data.bin

.PHONY: all run clean
