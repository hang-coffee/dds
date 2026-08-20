# Makefile - dcc：DOCTOR 的 C 子集编译器
#
# 用法:
#   make         构建 ./dcc
#   make clean   删除构建产物
#   make test    运行端到端测试（run_dcc_test.sh）
#
# 依赖: C++17 编译器（g++/clang++）

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

# 预处理器默认 include 目录：项目根/dcc/include
# （编译时注入绝对路径；运行时可用环境变量 DCC_INCLUDE 覆盖）
INCDIR   := $(CURDIR)/include
CXXFLAGS += -DDCC_INCLUDE_DIR='"$(INCDIR)"'

SRC      := $(wildcard src/*.cpp)
OBJ      := $(SRC:.cpp=.o)
TARGET   := dcc

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test: $(TARGET)
	sh run_dcc_test.sh

test-elf: $(TARGET)
	sh run_elf_test.sh

clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: clean test test-elf
