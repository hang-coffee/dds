#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "preprocessor.h"

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static char *exe_dir_of(const char *argv0) {
    const char *slash = strrchr(argv0, '/');
#ifdef _WIN32
    const char *bs = strrchr(argv0, '\\');
    if (!slash || (bs && bs > slash)) slash = bs;
#endif
    if (!slash) return dup_str(".");
    size_t n = (size_t)(slash - argv0);
    if (n == 0) return dup_str("/");
    char *d = (char *)malloc(n + 1);
    if (d) { memcpy(d, argv0, n); d[n] = 0; }
    return d;
}

static void build_lib_include_dir(PreprocessOptions *opt, const char *argv0) {
    char *exedir = exe_dir_of(argv0);
    const char *mode = opt->hosted ? "hosted" : "freestanding";
    size_t len = strlen(exedir) + strlen("/lib/") + strlen(mode) + strlen("/include") + 1;
    opt->lib_include_dir = (char *)malloc(len);
    if (opt->lib_include_dir)
        snprintf(opt->lib_include_dir, len, "%s/lib/%s/include", exedir, mode);
    free(exedir);
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static char *join_path(const char *dir, const char *name) {
    size_t dl = strlen(dir), nl = strlen(name);
    int need = dl > 0 && dir[dl - 1] != '/';
    char *p = (char *)malloc(dl + (need ? 1 : 0) + nl + 1);
    if (!p) return NULL;
    memcpy(p, dir, dl);
    size_t o = dl;
    if (need) p[o++] = '/';
    memcpy(p + o, name, nl);
    p[o + nl] = 0;
    return p;
}

static char *dir_name_of(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return dup_str(".");
    size_t n = (size_t)(slash - path);
    if (n == 0) return dup_str("/");
    char *d = (char *)malloc(n + 1);
    if (d) { memcpy(d, path, n); d[n] = 0; }
    return d;
}

static char *base_no_ext(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t n = dot ? (size_t)(dot - base) : strlen(base);
    char *p = (char *)malloc(n + 1);
    if (p) { memcpy(p, base, n); p[n] = 0; }
    return p;
}

static char *find_impl_for_header(const char *hdr) {
    char *stem = base_no_ext(hdr);
    char *stem_c = (char *)malloc(strlen(stem) + 3);
    if (stem_c) sprintf(stem_c, "%s.c", stem);
    char *result = NULL;
    const char *lib = strstr(hdr, "/lib/");
    if (!lib && strncmp(hdr, "lib/", 4) == 0) lib = hdr;
    if (lib) {
        const char *after_lib = (lib == hdr) ? hdr + 4 : lib + 5;
        const char *inc = strstr(after_lib, "/include/");
        if (!inc) inc = strstr(after_lib, "include/");
        if (inc) {
            size_t modlen = (size_t)(inc - after_lib);
            char *mod = (char *)malloc(modlen + 1);
            if (mod) { memcpy(mod, after_lib, modlen); mod[modlen] = 0; }
            char *libroot = NULL;
            if (lib == hdr) {
                libroot = dup_str(".");
            } else {
                size_t lrlen = (size_t)((lib - hdr) + 5);
                libroot = (char *)malloc(lrlen + 1);
                if (libroot) { memcpy(libroot, hdr, lrlen); libroot[lrlen] = 0; }
            }
            char *moddir = join_path(libroot, mod);
            char *stemc = join_path(moddir, stem_c);
            if (stemc && file_exists(stemc)) result = stemc;
            else {
                free(stemc);
                char *modc_name = (char *)malloc(strlen(mod) + 3);
                if (modc_name) { sprintf(modc_name, "%s.c", mod); }
                char *modc = join_path(moddir, modc_name ? modc_name : "");
                free(modc_name);
                if (modc && file_exists(modc)) result = modc;
                else free(modc);
            }
            free(moddir);
            free(libroot);
            free(mod);
            if (result) { free(stem); free(stem_c); return result; }
        }
    }
    /* 非 lib 头文件：在头文件所在目录寻找同名 .c */
    char *dir = dir_name_of(hdr);
    char *cand = join_path(dir, stem_c);
    if (cand && file_exists(cand)) result = cand;
    else free(cand);
    free(dir);
    free(stem);
    free(stem_c);
    return result;
}

static void program_merge(Program *dst, Program *src) {
    if (src->nglobals > 0) {
        dst->globals = (Global *)realloc(dst->globals, (size_t)(dst->nglobals + src->nglobals) * sizeof(Global));
        memcpy(dst->globals + dst->nglobals, src->globals, (size_t)src->nglobals * sizeof(Global));
        dst->nglobals += src->nglobals;
        free(src->globals);
        src->globals = NULL;
        src->nglobals = 0;
    }
    if (src->nfuncs > 0) {
        dst->funcs = (Function *)realloc(dst->funcs, (size_t)(dst->nfuncs + src->nfuncs) * sizeof(Function));
        memcpy(dst->funcs + dst->nfuncs, src->funcs, (size_t)src->nfuncs * sizeof(Function));
        dst->nfuncs += src->nfuncs;
        free(src->funcs);
        src->funcs = NULL;
        src->nfuncs = 0;
    }
    if (src->nstructs > 0) {
        dst->structs = (StructDef *)realloc(dst->structs, (size_t)(dst->nstructs + src->nstructs) * sizeof(StructDef));
        memcpy(dst->structs + dst->nstructs, src->structs, (size_t)src->nstructs * sizeof(StructDef));
        dst->nstructs += src->nstructs;
        free(src->structs);
        src->structs = NULL;
        src->nstructs = 0;
    }
}

static int list_contains(char **list, int n, const char *s) {
    for (int i = 0; i < n; i++)
        if (strcmp(list[i], s) == 0) return 1;
    return 0;
}

/* 判断 path 是否与某个显式输入文件为同一文件。
   dashash.cc 的 find_impl_for_header 返回的路径与输入路径通常写法一致；
   这里做“去掉 ./ 前缀”的轻度归一化后按字符串比较，覆盖常见情形。 */
static const char *strip_dot_slash(const char *p) {
    while (p[0] == '.' && p[1] == '/' && p[2]) p += 2;
    return p;
}
static int is_an_input_file(const char *path, char **inputs, int n) {
    if (!path || !*path) return 0;
    const char *a = strip_dot_slash(path);
    for (int i = 0; i < n; i++) {
        if (!inputs[i] || !*inputs[i]) continue;
        const char *b = strip_dot_slash(inputs[i]);
        if (strcmp(a, b) == 0) return 1;
    }
    return 0;
}

static char *replace_ext(const char *path, const char *ext) {
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    size_t base_len = (dot && (!slash || dot > slash)) ? (size_t)(dot - path) : strlen(path);
    size_t ext_len = strlen(ext);
    char *p = (char *)malloc(base_len + ext_len + 1);
    if (p) { memcpy(p, path, base_len); memcpy(p + base_len, ext, ext_len + 1); }
    return p;
}

static void print_help(const char *prog) {
    printf("用法: %s [选项] <input.c> [more.c ...] [output]\n", prog);
    printf("\n选项:\n");
    printf("  -o, --output <file>        指定输出文件\n");
    printf("  -m, --format <fmt>         输出格式: asm, bin, elf (默认 asm)\n");
    printf("      --format=<fmt>         同上\n");
    printf("  -I<dir>, -I <dir>          添加头文件搜索目录\n");
    printf("  -ffreestanding             使用 freestanding 库头文件目录\n");
    printf("  -fhosted                   使用 hosted 库头文件目录 (默认)\n");
    printf("  -h, --help                 显示本帮助信息\n");
    printf("\n输出格式:\n");
    printf("  asm  生成 DOCTOR DASM 汇编\n");
    printf("  elf  生成 ELF32 relocatable 目标文件 (.o)\n");
    printf("  bin  生成平坦二进制 (xxx_code.bin / xxx_data.bin)\n");
}

int main(int argc, char **argv) {
    PreprocessOptions opt;
    preprocess_options_init(&opt);
    const char *format = "asm";
    const char *out_opt = NULL;
    int input_indices[256];
    int ninputs = 0;
    int output_idx = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-ffreestanding") == 0) {
            opt.hosted = 0;
        } else if (strcmp(argv[i], "-fhosted") == 0) {
            opt.hosted = 1;
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--format") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "dcc: %s 缺少格式参数\n", argv[i]);
                preprocess_options_free(&opt);
                return 1;
            }
            format = argv[++i];
        } else if (strncmp(argv[i], "--format=", 9) == 0) {
            format = argv[i] + 9;
        } else if (strncmp(argv[i], "-m", 2) == 0 && argv[i][2] != 0) {
            format = argv[i] + 2;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "dcc: %s 缺少输出文件\n", argv[i]);
                preprocess_options_free(&opt);
                return 1;
            }
            out_opt = argv[++i];
        } else if (strncmp(argv[i], "--output=", 9) == 0) {
            out_opt = argv[i] + 9;
        } else if (strncmp(argv[i], "-I", 2) == 0) {
            const char *dir = argv[i] + 2;
            if (!*dir && i + 1 < argc) dir = argv[++i];
            if (!*dir) {
                fprintf(stderr, "dcc: -I 缺少目录\n");
                preprocess_options_free(&opt);
                return 1;
            }
            preprocess_options_add_include_dir(&opt, dir);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            preprocess_options_free(&opt);
            return 0;
        } else if (argv[i][0] == '-' && strcmp(argv[i], "-") != 0) {
            fprintf(stderr, "dcc: 未知选项 %s\n", argv[i]);
            preprocess_options_free(&opt);
            return 1;
        } else {
            size_t alen = strlen(argv[i]);
            int looks_out = alen >= 4 &&
                (strcmp(argv[i] + alen - 4, ".asm") == 0 ||
                 strcmp(argv[i] + alen - 4, ".bin") == 0 ||
                 strcmp(argv[i] + alen - 2, ".o") == 0);
            if (!out_opt && output_idx < 0 && ninputs > 0 && looks_out) {
                output_idx = i;
            } else if (ninputs < 256) {
                input_indices[ninputs++] = i;
            } else {
                fprintf(stderr, "dcc: 输入文件过多\n");
                preprocess_options_free(&opt);
                return 1;
            }
        }
    }

    if (strcmp(format, "asm") != 0 && strcmp(format, "bin") != 0 && strcmp(format, "elf") != 0) {
        fprintf(stderr, "dcc: 未知输出格式 '%s'\n", format);
        preprocess_options_free(&opt);
        return 1;
    }
    char *derived_out = NULL;
    if (ninputs == 0) {
        fprintf(stderr, "用法: %s [选项] input.c [more.c ...] [output]\n", argv[0]);
        preprocess_options_free(&opt);
        return 1;
    }
    const char *out_path = out_opt ? out_opt : (output_idx >= 0 ? argv[output_idx] : NULL);
    if (!out_path) {
        if (strcmp(format, "elf") == 0) derived_out = replace_ext(argv[input_indices[0]], ".o");
        else if (strcmp(format, "bin") == 0) derived_out = replace_ext(argv[input_indices[0]], ".bin");
        else derived_out = replace_ext(argv[input_indices[0]], ".asm");
        out_path = derived_out;
    }

    build_lib_include_dir(&opt, argv[0]);

    char *err = NULL;
    char **impls = NULL;
    int nimpls = 0, capimpls = 0;
    Program prog;
    memset(&prog, 0, sizeof(prog));

    for (int fi = 0; fi < ninputs; fi++) {
        PreprocessResult ir = preprocess_file_result(argv[input_indices[fi]], &opt, &err);
        if (!ir.source) {
            fprintf(stderr, "dcc: %s\n", err ? err : "预处理失败");
            free(err);
            preprocess_result_free(&ir);
            program_free(&prog);
            for (int j = 0; j < nimpls; j++) free(impls[j]);
            free(impls);
            preprocess_options_free(&opt);
            return 1;
        }
        /* ELF 模式不自动合并头文件的实现：每个 .o 只包含自身的定义，
           实现留作外部引用，由 dlinker 在链接期解析，避免重复定义。 */
        if (strcmp(format, "elf") != 0) {
            for (int j = 0; j < ir.nheaders; j++) {
                char *impl = find_impl_for_header(ir.headers[j]);
                if (impl && !list_contains(impls, nimpls, impl) &&
                    !is_an_input_file(impl, (char **)argv + input_indices[0], ninputs)) {
                    if (nimpls >= capimpls) {
                        capimpls = capimpls ? capimpls * 2 : 8;
                        impls = (char **)realloc(impls, (size_t)capimpls * sizeof(char *));
                    }
                    impls[nimpls++] = impl;
                } else {
                    free(impl);
                }
            }
        }
        TokenArray ta = tokenize(ir.source);
        Program ip = parse_program(&ta, &err);
        token_array_free(&ta);
        preprocess_result_free(&ir);
        if (err) {
            fprintf(stderr, "dcc: %s\n", err);
            parse_error_free(err);
            program_free(&prog);
            program_free(&ip);
            for (int j = 0; j < nimpls; j++) free(impls[j]);
            free(impls);
            preprocess_options_free(&opt);
            return 1;
        }
        program_merge(&prog, &ip);
        program_free(&ip);
    }

    for (int i = 0; i < nimpls; i++) {
        PreprocessResult ir = preprocess_file_result(impls[i], &opt, &err);
        if (!ir.source) {
            fprintf(stderr, "dcc: %s\n", err ? err : "预处理失败");
            free(err);
            program_free(&prog);
            preprocess_result_free(&ir);
            for (int j = 0; j < nimpls; j++) free(impls[j]);
            free(impls);
            preprocess_options_free(&opt);
            return 1;
        }
        if (strcmp(format, "elf") != 0) {
            for (int j = 0; j < ir.nheaders; j++) {
                char *impl2 = find_impl_for_header(ir.headers[j]);
                if (impl2 && !list_contains(impls, nimpls, impl2) &&
                    !is_an_input_file(impl2, (char **)argv + input_indices[0], ninputs)) {
                    if (nimpls >= capimpls) {
                        capimpls = capimpls ? capimpls * 2 : 8;
                        impls = (char **)realloc(impls, (size_t)capimpls * sizeof(char *));
                    }
                    impls[nimpls++] = impl2;
                } else {
                    free(impl2);
                }
            }
        }
        TokenArray ita = tokenize(ir.source);
        Program ip = parse_program(&ita, &err);
        token_array_free(&ita);
        preprocess_result_free(&ir);
        if (err) {
            fprintf(stderr, "dcc: %s\n", err);
            parse_error_free(err);
            program_free(&prog);
            program_free(&ip);
            for (int j = 0; j < nimpls; j++) free(impls[j]);
            free(impls);
            preprocess_options_free(&opt);
            return 1;
        }
        program_merge(&prog, &ip);
        program_free(&ip);
    }

    for (int i = 0; i < nimpls; i++) free(impls[i]);
    free(impls);
    preprocess_options_free(&opt);

    int ok = 0;
    if (strcmp(format, "asm") == 0) {
        ok = generate_code(&prog, out_path, 0, &err);
    } else {
        char *tmp_asm = (char *)malloc(strlen(out_path) + 16);
        sprintf(tmp_asm, "%s.tmp.asm", out_path);
        ok = generate_code(&prog, tmp_asm, strcmp(format, "elf") == 0, &err);
        if (ok) {
            char *exedir = exe_dir_of(argv[0]);
            char dasm_path[1024];
            snprintf(dasm_path, sizeof(dasm_path), "%s/../dasm/dasm", exedir);
            free(exedir);
            char cmd[2048];
            if (strcmp(format, "elf") == 0) {
                snprintf(cmd, sizeof(cmd), "%s -m elf %s %s > /dev/null 2>&1", dasm_path, tmp_asm, out_path);
            } else {
                char *base = replace_ext(out_path, "");
                char *code_file = (char *)malloc(strlen(base) + 16);
                char *data_file = (char *)malloc(strlen(base) + 16);
                sprintf(code_file, "%s_code.bin", base);
                sprintf(data_file, "%s_data.bin", base);
                snprintf(cmd, sizeof(cmd), "%s %s %s %s > /dev/null 2>&1", dasm_path, tmp_asm, code_file, data_file);
                free(base);
                free(code_file);
                free(data_file);
            }
            int rc = system(cmd);
            remove(tmp_asm);
            if (rc != 0) {
                fprintf(stderr, "dcc: dasm 汇编失败\n");
                free(tmp_asm);
                free(derived_out);
                program_free(&prog);
                free(err);
                return 1;
            }
        }
        free(tmp_asm);
    }
    if (!ok) {
        fprintf(stderr, "dcc: %s\n", err ? err : "代码生成失败");
        free(err);
        free(derived_out);
        program_free(&prog);
        return 1;
    }
    program_free(&prog);
    if (ninputs == 1)
        printf("dcc: %s -> %s (ok, format=%s)\n", argv[input_indices[0]], out_path, format);
    else
        printf("dcc: %d 个输入文件 -> %s (ok, format=%s)\n", ninputs, out_path, format);
    free(derived_out);
    return 0;
}
