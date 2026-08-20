# Makefile - dda：DOCTOR ELF -> DASM 反汇编器
CC       ?= gcc
CFLAGS   ?= -std=c99 -O2 -Wall -Wextra
TARGET   := dda
SRC      := src/dda.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

.PHONY: clean
