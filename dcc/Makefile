# dcc - DOCTOR C 编译器（C99 实现）
CC ?= gcc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Isrc
SRC := src/main.c src/lexer.c src/parser.c src/ast.c src/codegen.c src/preprocessor.c
OBJ := $(SRC:.c=.o)

all: dcc

dcc: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	./dcc tests/hello.c /tmp/dcc-hello.asm
	../dasm/dasm /tmp/dcc-hello.asm /tmp/dcc-hello.code /tmp/dcc-hello.data
	./dcc tests/types.c /tmp/dcc-types.asm
	../dasm/dasm /tmp/dcc-types.asm /tmp/dcc-types.code /tmp/dcc-types.data
	./dcc tests/long64.c /tmp/dcc-long64.asm
	../dasm/dasm /tmp/dcc-long64.asm /tmp/dcc-long64.code /tmp/dcc-long64.data
	./dcc tests/signed_types.c /tmp/dcc-signed_types.asm
	../dasm/dasm /tmp/dcc-signed_types.asm /tmp/dcc-signed_types.code /tmp/dcc-signed_types.data
	./dcc tests/float_test.c /tmp/dcc-float_test.asm
	../dasm/dasm /tmp/dcc-float_test.asm /tmp/dcc-float_test.code /tmp/dcc-float_test.data
	./dcc tests/fp_conv.c /tmp/dcc-fp_conv.asm
	../dasm/dasm /tmp/dcc-fp_conv.asm /tmp/dcc-fp_conv.code /tmp/dcc-fp_conv.data
	./dcc tests/bool_test.c /tmp/dcc-bool_test.asm
	../dasm/dasm /tmp/dcc-bool_test.asm /tmp/dcc-bool_test.code /tmp/dcc-bool_test.data
	./dcc tests/volatile_test.c /tmp/dcc-volatile_test.asm
	../dasm/dasm /tmp/dcc-volatile_test.asm /tmp/dcc-volatile_test.code /tmp/dcc-volatile_test.data
	./dcc tests/pointer_array.c /tmp/dcc-pointer_array.asm
	../dasm/dasm /tmp/dcc-pointer_array.asm /tmp/dcc-pointer_array.code /tmp/dcc-pointer_array.data
	./dcc tests/multidim_array.c /tmp/dcc-multidim_array.asm
	../dasm/dasm /tmp/dcc-multidim_array.asm /tmp/dcc-multidim_array.code /tmp/dcc-multidim_array.data
	./dcc tests/preprocess_test.c /tmp/dcc-preprocess_test.asm
	../dasm/dasm /tmp/dcc-preprocess_test.asm /tmp/dcc-preprocess_test.code /tmp/dcc-preprocess_test.data
	./dcc tests/cond_compile.c /tmp/dcc-cond_compile.asm
	../dasm/dasm /tmp/dcc-cond_compile.asm /tmp/dcc-cond_compile.code /tmp/dcc-cond_compile.data
	./dcc tests/reg_asm.c /tmp/dcc-reg_asm.asm
	../dasm/dasm /tmp/dcc-reg_asm.asm /tmp/dcc-reg_asm.code /tmp/dcc-reg_asm.data
	./dcc tests/string_literal.c /tmp/dcc-string_literal.asm
	../dasm/dasm /tmp/dcc-string_literal.asm /tmp/dcc-string_literal.code /tmp/dcc-string_literal.data
	./dcc tests/struct_test.c /tmp/dcc-struct_test.asm
	../dasm/dasm /tmp/dcc-struct_test.asm /tmp/dcc-struct_test.code /tmp/dcc-struct_test.data
	./dcc tests/typedef_union_enum.c /tmp/dcc-typedef_union_enum.asm
	../dasm/dasm /tmp/dcc-typedef_union_enum.asm /tmp/dcc-typedef_union_enum.code /tmp/dcc-typedef_union_enum.data
	./dcc tests/funcptr_void.c /tmp/dcc-funcptr_void.asm
	../dasm/dasm /tmp/dcc-funcptr_void.asm /tmp/dcc-funcptr_void.code /tmp/dcc-funcptr_void.data
	./dcc tests/auto_impl.c /tmp/dcc-auto_impl.asm
	../dasm/dasm /tmp/dcc-auto_impl.asm /tmp/dcc-auto_impl.code /tmp/dcc-auto_impl.data
	./dcc tests/sizeof_init_list.c /tmp/dcc-sizeof_init_list.asm
	../dasm/dasm /tmp/dcc-sizeof_init_list.asm /tmp/dcc-sizeof_init_list.code /tmp/dcc-sizeof_init_list.data
	./dcc tests/multi_a.c tests/multi_b.c /tmp/dcc-multi.asm
	../dasm/dasm /tmp/dcc-multi.asm /tmp/dcc-multi.code /tmp/dcc-multi.data
	./dcc tests/storage_a.c tests/storage_b.c /tmp/dcc-storage.asm
	../dasm/dasm /tmp/dcc-storage.asm /tmp/dcc-storage.code /tmp/dcc-storage.data
	./dcc tests/control_flow.c /tmp/dcc-control_flow.asm
	../dasm/dasm /tmp/dcc-control_flow.asm /tmp/dcc-control_flow.code /tmp/dcc-control_flow.data
	./dcc tests/isr_static.c /tmp/dcc-isr_static.asm
	../dasm/dasm /tmp/dcc-isr_static.asm /tmp/dcc-isr_static.code /tmp/dcc-isr_static.data
	./dcc tests/vararg_switch.c /tmp/dcc-vararg_switch.asm
	../dasm/dasm /tmp/dcc-vararg_switch.asm /tmp/dcc-vararg_switch.code /tmp/dcc-vararg_switch.data

test-elf: all
	sh run_elf_test.sh

clean:
	rm -f dcc $(OBJ) /tmp/dcc-hello.* /tmp/dcc-types.* /tmp/dcc-long64.* /tmp/dcc-signed_types.* /tmp/dcc-float_test.* /tmp/dcc-fp_conv.* /tmp/dcc-bool_test.* /tmp/dcc-volatile_test.* /tmp/dcc-pointer_array.* /tmp/dcc-multidim_array.* /tmp/dcc-preprocess_test.* /tmp/dcc-cond_compile.* /tmp/dcc-reg_asm.* /tmp/dcc-string_literal.* /tmp/dcc-struct_test.* /tmp/dcc-typedef_union_enum.* /tmp/dcc-funcptr_void.* /tmp/dcc-auto_impl.* /tmp/dcc-sizeof_init_list.* /tmp/dcc-multi.* /tmp/dcc-storage.* /tmp/dcc-control_flow.* /tmp/dcc-isr_static.* /tmp/dcc-vararg_switch.*

.PHONY: all test test-elf clean
