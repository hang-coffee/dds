# Makefile - dlinker：DOCTOR 最小链接器（C99 实现）
CC       ?= gcc
CFLAGS   ?= -std=c99 -O2 -Wall -Wextra
TARGET   := dlinker
SRC      := src/main.c
OBJ      := $(SRC:.c=.o)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TARGET)
	sh tests/run_dlinker_test.sh

clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: clean test
