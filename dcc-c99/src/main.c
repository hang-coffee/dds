#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"
#include "codegen.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "用法: %s input.c output.asm\n", argv[0]);
        return 1;
    }
    FILE *in = fopen(argv[1], "rb");
    if (!in) {
        fprintf(stderr, "无法打开输入文件: %s\n", argv[1]);
        return 1;
    }
    fseek(in, 0, SEEK_END);
    long sz = ftell(in);
    fseek(in, 0, SEEK_SET);
    char *src = (char *)malloc((size_t)sz + 1);
    if (sz > 0) fread(src, 1, (size_t)sz, in);
    src[sz] = 0;
    fclose(in);

    TokenArray ta = tokenize(src);
    free(src);

    char *err = NULL;
    Program prog = parse_program(&ta, &err);
    token_array_free(&ta);
    if (err) {
        fprintf(stderr, "dcc-c99: %s\n", err);
        parse_error_free(err);
        program_free(&prog);
        return 1;
    }

    if (!generate_code(&prog, argv[2], &err)) {
        fprintf(stderr, "dcc-c99: %s\n", err ? err : "代码生成失败");
        free(err);
        program_free(&prog);
        return 1;
    }
    program_free(&prog);
    printf("dcc-c99: %s -> %s (ok)\n", argv[1], argv[2]);
    return 0;
}
