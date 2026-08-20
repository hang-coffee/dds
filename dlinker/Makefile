# Makefile - dlinker：DOCTOR 最小链接器
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
TARGET   := dlinker
SRC      := src/main.cpp
OBJ      := $(SRC:.cpp=.o)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test: $(TARGET)
	sh tests/run_dlinker_test.sh

clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: clean test
