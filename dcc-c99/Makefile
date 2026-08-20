# dcc-c99 - dcc 的 C99 重写（最小实现）
CC ?= gcc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Isrc
SRC := src/main.c src/lexer.c src/parser.c src/ast.c src/codegen.c
OBJ := $(SRC:.c=.o)

all: dcc-c99

dcc-c99: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	./dcc-c99 tests/hello.c /tmp/dcc-c99-hello.asm
	../dasm/dasm /tmp/dcc-c99-hello.asm /tmp/dcc-c99-hello.code /tmp/dcc-c99-hello.data
	./dcc-c99 tests/types.c /tmp/dcc-c99-types.asm
	../dasm/dasm /tmp/dcc-c99-types.asm /tmp/dcc-c99-types.code /tmp/dcc-c99-types.data
	./dcc-c99 tests/long64.c /tmp/dcc-c99-long64.asm
	../dasm/dasm /tmp/dcc-c99-long64.asm /tmp/dcc-c99-long64.code /tmp/dcc-c99-long64.data
	./dcc-c99 tests/signed_types.c /tmp/dcc-c99-signed_types.asm
	../dasm/dasm /tmp/dcc-c99-signed_types.asm /tmp/dcc-c99-signed_types.code /tmp/dcc-c99-signed_types.data

clean:
	rm -f dcc-c99 $(OBJ) /tmp/dcc-c99-hello.* /tmp/dcc-c99-types.* /tmp/dcc-c99-long64.* /tmp/dcc-c99-signed_types.*

.PHONY: all test clean
