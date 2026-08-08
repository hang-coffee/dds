# ============================================================
# DOCTOR 模拟器 Makefile
# ============================================================

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -g -Isrc -MMD -MP
LDFLAGS  = -lm

TARGET   = build/bin/doctor_sim
SRC_DIR  = src
BUILD_DIR = build
OBJ_DIR  = $(BUILD_DIR)/obj

# 自动收集所有 .c 源文件（包括子目录）
SOURCES := $(shell find $(SRC_DIR) -name "*.c")
OBJECTS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))
DEPS    := $(OBJECTS:.o=.d)

# 默认目标
all: $(TARGET)

# 链接最终可执行文件
$(TARGET): $(OBJECTS) | $(BUILD_DIR)/bin
	$(CC) $^ -o $@ $(LDFLAGS)

# 编译每个 .c 到 .o（自动创建子目录）
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# 创建必要的目录
$(BUILD_DIR)/bin $(OBJ_DIR):
	mkdir -p $@

# 清理编译产物
clean:
	rm -rf $(BUILD_DIR)

# 运行（默认加载 tests/bin/test.bin）
run: $(TARGET)
	./$(TARGET) -f tests/bin/test.bin

# 调试模式（带 DEBUG 宏，开启 INFO 日志）
debug: CFLAGS += -DDEBUG
debug: clean $(TARGET)

# 查看每个目标文件的依赖关系
show-deps:
	@echo "SOURCES: $(SOURCES)"
	@echo "OBJECTS: $(OBJECTS)"

# 包含自动生成的依赖文件（-MMD 生成的 .d）
-include $(DEPS)

.PHONY: all clean run debug show-deps
