# ============================================================================
# DASM 汇编器 — Makefile (健壮版)
# ============================================================================

# ----- 工具链 -----------------------------------------------------------------
CXX      := g++
CXXFLAGS := -std=c++14 -Wall -Wextra -MMD -MP -O2 -g #-DDEBUG
LDFLAGS  :=

# ----- 目录（必须显式赋值）---------------------------------------------------
SRC_DIR   := src
BUILD_DIR := build
TEST_DIR  := tests
BIN_DIR   := .

# ----- 目标文件 ---------------------------------------------------------------
TARGET    := $(BIN_DIR)/dasm
TEST_TARGET := $(BIN_DIR)/dasm_test

# 扫描源文件（注意：这里要放在 SRC_DIR 赋值之后）
SOURCES   := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS   := $(SOURCES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS      := $(OBJECTS:.o=.d)

TEST_SOURCES := $(wildcard $(TEST_DIR)/*.cpp)
TEST_OBJECTS := $(TEST_SOURCES:$(TEST_DIR)/%.cpp=$(BUILD_DIR)/test_%.o)
TEST_LINK_OBJS := $(filter-out $(BUILD_DIR)/main.o, $(OBJECTS)) $(TEST_OBJECTS)

# 修正：把 -I 放到 CXXFLAGS 里，确保 SRC_DIR 已定义
CXXFLAGS += -I$(SRC_DIR)

# ----- 默认目标 ---------------------------------------------------------------
.PHONY: all clean test

all: $(TARGET)

# ----- 检查是否有源文件 -------------------------------------------------------
ifeq ($(strip $(SOURCES)),)
$(error 错误: src/ 目录下没有任何 .cpp 源文件。请至少创建 main.cpp)
endif

# ----- 链接主程序 -------------------------------------------------------------
$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# ----- 编译 src/*.cpp -> build/*.o --------------------------------------------
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ----- 编译 tests/*.cpp -> build/test_*.o -------------------------------------
$(BUILD_DIR)/test_%.o: $(TEST_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ----- 创建 build 目录 --------------------------------------------------------
$(BUILD_DIR):
	mkdir -p $@

# ----- 测试 -------------------------------------------------------------------
test: $(TEST_TARGET)
	@echo "========== 运行测试 =========="
	./$(TEST_TARGET)
	@echo "========== 测试完成 =========="

$(TEST_TARGET): $(TEST_LINK_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# ----- 自动包含依赖文件 -------------------------------------------------------
-include $(DEPS)

# ----- 清理 -------------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(TEST_TARGET)
