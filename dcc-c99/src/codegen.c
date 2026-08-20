#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    char *name;
    int offset;
    int size;
    int is_unsigned;
} VarInfo;

typedef struct {
    char *name;
    int offset;
    int size;
    int is_unsigned;
} ParamInfo;

typedef struct {
    char *brk;
    char *cont;
} LoopInfo;

typedef struct {
    FILE *out;
    Program *prog;
    int label;
    VarInfo *locals;
    int nlocals, caplocals;
    ParamInfo *params;
    int nparams, capparams;
    LoopInfo *loops;
    int nloops, caploops;
    int frame_size;
} CodeGen;

static VarInfo *local_info(CodeGen *cg, const char *name);
static ParamInfo *param_info(CodeGen *cg, const char *name);
static void emit_store_to_b(CodeGen *cg, int size);
static void emit_load_from_b(CodeGen *cg, int size, int is_unsigned);

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void cg_emit(CodeGen *cg, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputc('\t', cg->out);
    vfprintf(cg->out, fmt, ap);
    fputc('\n', cg->out);
    va_end(ap);
}

static char *cg_new_label(CodeGen *cg, const char *prefix) {
    char *buf = (char *)malloc(64);
    cg->label++;
    snprintf(buf, 64, "%s%d", prefix, cg->label);
    return buf;
}

static int func_exists(Program *p, const char *name) {
    for (int i = 0; i < p->nfuncs; i++)
        if (strcmp(p->funcs[i].name, name) == 0) return 1;
    return 0;
}

static int global_exists(Program *p, const char *name) {
    for (int i = 0; i < p->nglobals; i++)
        if (strcmp(p->globals[i].name, name) == 0) return 1;
    return 0;
}

static Global *global_info(Program *p, const char *name) {
    for (int i = 0; i < p->nglobals; i++)
        if (strcmp(p->globals[i].name, name) == 0) return &p->globals[i];
    return NULL;
}

static int var_size(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 4;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->size;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->size;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->type_size;
    return 4;
}

static int var_unsigned(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 0;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->is_unsigned;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->is_unsigned;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->is_unsigned;
    return 0;
}

static int expr_size(CodeGen *cg, Expr *e);
static int expr_unsigned(CodeGen *cg, Expr *e);

static void cg_push_local(CodeGen *cg, const char *name, int offset, int size, int is_unsigned) {
    if (cg->nlocals >= cg->caplocals) {
        cg->caplocals = cg->caplocals ? cg->caplocals * 2 : 16;
        cg->locals = (VarInfo *)realloc(cg->locals, (size_t)cg->caplocals * sizeof(VarInfo));
    }
    cg->locals[cg->nlocals].name = xstrdup(name);
    cg->locals[cg->nlocals].offset = offset;
    cg->locals[cg->nlocals].size = size;
    cg->locals[cg->nlocals].is_unsigned = is_unsigned;
    cg->nlocals++;
}

static int local_offset(CodeGen *cg, const char *name) {
    for (int i = 0; i < cg->nlocals; i++)
        if (strcmp(cg->locals[i].name, name) == 0) return cg->locals[i].offset;
    return -1;
}

static VarInfo *local_info(CodeGen *cg, const char *name) {
    for (int i = 0; i < cg->nlocals; i++)
        if (strcmp(cg->locals[i].name, name) == 0) return &cg->locals[i];
    return NULL;
}

static void cg_push_param(CodeGen *cg, const char *name, int offset, int size, int is_unsigned) {
    if (cg->nparams >= cg->capparams) {
        cg->capparams = cg->capparams ? cg->capparams * 2 : 8;
        cg->params = (ParamInfo *)realloc(cg->params, (size_t)cg->capparams * sizeof(ParamInfo));
    }
    cg->params[cg->nparams].name = xstrdup(name);
    cg->params[cg->nparams].offset = offset;
    cg->params[cg->nparams].size = size;
    cg->params[cg->nparams].is_unsigned = is_unsigned;
    cg->nparams++;
}

static int param_offset(CodeGen *cg, const char *name) {
    for (int i = 0; i < cg->nparams; i++)
        if (strcmp(cg->params[i].name, name) == 0) return cg->params[i].offset;
    return -1;
}

static ParamInfo *param_info(CodeGen *cg, const char *name) {
    for (int i = 0; i < cg->nparams; i++)
        if (strcmp(cg->params[i].name, name) == 0) return &cg->params[i];
    return NULL;
}

static void cg_push_loop(CodeGen *cg, const char *brk, const char *cont) {
    if (cg->nloops >= cg->caploops) {
        cg->caploops = cg->caploops ? cg->caploops * 2 : 8;
        cg->loops = (LoopInfo *)realloc(cg->loops, (size_t)cg->caploops * sizeof(LoopInfo));
    }
    cg->loops[cg->nloops].brk = xstrdup(brk);
    cg->loops[cg->nloops].cont = xstrdup(cont);
    cg->nloops++;
}

static void cg_pop_loop(CodeGen *cg) {
    if (cg->nloops > 0) {
        cg->nloops--;
        free(cg->loops[cg->nloops].brk);
        free(cg->loops[cg->nloops].cont);
    }
}

static void gen_expr(CodeGen *cg, Expr *e);
static void gen_stmt(CodeGen *cg, Stmt *s);

static void collect_locals(CodeGen *cg, Stmt **stmts, int n, int *frame) {
    for (int i = 0; i < n; i++) {
        Stmt *s = stmts[i];
        if (s->kind == STMT_DECL) {
            int sz = s->decl_size > 0 ? s->decl_size : 4;
            cg_push_local(cg, s->name, *frame, sz, s->decl_unsigned);
            *frame += sz;
        } else if (s->kind == STMT_BLOCK) {
            collect_locals(cg, s->items, s->nitems, frame);
        } else if (s->kind == STMT_IF) {
            if (s->then) { Stmt *tmp[1] = {s->then}; collect_locals(cg, tmp, 1, frame); }
            if (s->els) { Stmt *tmp[1] = {s->els}; collect_locals(cg, tmp, 1, frame); }
        } else if (s->kind == STMT_WHILE) {
            if (s->body) { Stmt *tmp[1] = {s->body}; collect_locals(cg, tmp, 1, frame); }
        } else if (s->kind == STMT_FOR) {
            if (s->body) { Stmt *tmp[1] = {s->body}; collect_locals(cg, tmp, 1, frame); }
        }
    }
}

static void var_addr(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) { fprintf(stderr, "左值必须是变量\n"); exit(1); }
    int lo = local_offset(cg, e->name);
    int po = param_offset(cg, e->name);
    if (lo >= 0) {
        cg_emit(cg, "MOV A, F");
        cg_emit(cg, "ADD DWORD A, %d", lo);
    } else if (po >= 0) {
        cg_emit(cg, "MOV A, F");
        cg_emit(cg, "SUB DWORD A, %d", po);
    } else if (global_exists(cg->prog, e->name)) {
        cg_emit(cg, "LET A, DWORD var_%s", e->name);
    } else {
        fprintf(stderr, "line %d: 未定义变量 %s\n", e->line, e->name);
        exit(1);
    }
}

static void gen_incdec(CodeGen *cg, Expr *e, int postfix) {
    Expr *target = e->r ? e->r : e->l;
    var_addr(cg, target);
    cg_emit(cg, "MOV B, A");
    emit_load_from_b(cg, var_size(cg, target), var_unsigned(cg, target));
    if (postfix) cg_emit(cg, "PUSH DWORD A");
    if (strcmp(e->op, "+") == 0) cg_emit(cg, "ADD DWORD A, 1");
    else cg_emit(cg, "SUB DWORD A, 1");
    emit_store_to_b(cg, var_size(cg, target));
    if (postfix) cg_emit(cg, "POP DWORD A");
}

static void gen_call(CodeGen *cg, Expr *e) {
    if (!e->name || !func_exists(cg->prog, e->name)) {
        fprintf(stderr, "line %d: 未定义函数 %s\n", e->line, e->name ? e->name : "?");
        exit(1);
    }
    int argbytes = 4 * e->nargs;
    cg_emit(cg, "MOV A, F");
    cg_emit(cg, "PUSH DWORD A");
    for (int i = 0; i < e->nargs; i++) {
        gen_expr(cg, e->args[i]);
        if (expr_size(cg, e->args[i]) == 8) {
            cg_emit(cg, "PUSH DWORD A");
            cg_emit(cg, "PUSH DWORD D1");
            argbytes += 4;
        } else {
            cg_emit(cg, "PUSH DWORD A");
        }
    }
    const char *ret = cg_new_label(cg, "RET");
    cg_emit(cg, "LET E, DWORD %s", ret);
    cg_emit(cg, "PUSH DWORD E");
    cg_emit(cg, "LET E, DWORD func_%s", e->name);
    cg_emit(cg, "JMP");
    cg_emit(cg, "%s:", ret);
    if (argbytes) cg_emit(cg, "SUB DWORD S, %d", argbytes);
    cg_emit(cg, "POP DWORD F");
    int rsz = 4;
    for (int i = 0; i < cg->prog->nfuncs; i++)
        if (strcmp(cg->prog->funcs[i].name, e->name) == 0)
            rsz = cg->prog->funcs[i].ret_size;
    if (rsz != 8)
        cg_emit(cg, "MOV A, D1");
}


/* ---------- 64 位整数运算辅助 ---------- */
/* 约定：A=低32位，D1=高32位；右操作数在 B=低32位，R=高32位。 */

static void emit_long_add(CodeGen *cg) {
    const char *lno = cg_new_label(cg, "LA");
    cg_emit(cg, "ADD DWORD A, B");
    cg_emit(cg, "MOV C, A");
    cg_emit(cg, "LET E, DWORD %s", lno);
    cg_emit(cg, "JNB DWORD B");
    cg_emit(cg, "INC D1");
    cg_emit(cg, "%s:", lno);
    cg_emit(cg, "ADD DWORD D1, R");
}

static void emit_long_sub(CodeGen *cg) {
    const char *lno = cg_new_label(cg, "LS");
    cg_emit(cg, "MOV C, A");
    cg_emit(cg, "LET E, DWORD %s", lno);
    cg_emit(cg, "JNB DWORD B");
    cg_emit(cg, "SUB DWORD D1, 1");
    cg_emit(cg, "%s:", lno);
    cg_emit(cg, "SUB DWORD A, B");
    cg_emit(cg, "SUB DWORD D1, R");
}

static void emit_long_mul(CodeGen *cg) {
    cg_emit(cg, "PUSH DWORD D1");
    cg_emit(cg, "MUL DWORD A, B");
    cg_emit(cg, "MOV C, D1");
    cg_emit(cg, "PUSH DWORD D2");
    cg_emit(cg, "MUL DWORD A, R");
    cg_emit(cg, "ADD DWORD C, D2");
    cg_emit(cg, "POP DWORD D2");
    cg_emit(cg, "MOV A, D2");
    cg_emit(cg, "POP DWORD D2");
    cg_emit(cg, "PUSH DWORD A");
    cg_emit(cg, "MUL DWORD D2, B");
    cg_emit(cg, "ADD DWORD C, D2");
    cg_emit(cg, "POP DWORD A");
    cg_emit(cg, "MOV D1, C");
}

static void emit_long_neg(CodeGen *cg) {
    const char *lnz = cg_new_label(cg, "LN");
    const char *ld = cg_new_label(cg, "LN");
    cg_emit(cg, "MNE DWORD A");
    cg_emit(cg, "MOV C, A");
    cg_emit(cg, "ZERO T");
    cg_emit(cg, "CMP DWORD T");
    cg_emit(cg, "LET E, DWORD %s", lnz);
    cg_emit(cg, "JZ");
    cg_emit(cg, "NEG D1");
    cg_emit(cg, "LET E, DWORD %s", ld);
    cg_emit(cg, "JMP");
    cg_emit(cg, "%s:", lnz);
    cg_emit(cg, "MNE DWORD D1");
    cg_emit(cg, "%s:", ld);
}

static void emit_long_not(CodeGen *cg) {
    cg_emit(cg, "NEG A");
    cg_emit(cg, "NEG D1");
}

static void emit_long_udiv(CodeGen *cg) {
    const char *lnz = cg_new_label(cg, "UZ");
    cg_emit(cg, "ZERO T");
    cg_emit(cg, "MOV C, B");
    cg_emit(cg, "CMP DWORD T");
    cg_emit(cg, "LET E, DWORD %s", lnz);
    cg_emit(cg, "JNZ");
    cg_emit(cg, "MOV C, R");
    cg_emit(cg, "CMP DWORD T");
    cg_emit(cg, "LET E, DWORD %s", lnz);
    cg_emit(cg, "JNZ");
    cg_emit(cg, "DIV DWORD A, B");
    cg_emit(cg, "%s:", lnz);
    cg_emit(cg, "PUSH DWORD X");
    cg_emit(cg, "PUSH DWORD I");
    cg_emit(cg, "ZERO X");
    cg_emit(cg, "ZERO I");
    cg_emit(cg, "LET C, DWORD 64");
    cg_emit(cg, "PUSH DWORD C");
    cg_emit(cg, "ZERO T");
    const char *lp = cg_new_label(cg, "UD");
    cg_emit(cg, "%s:", lp);
    cg_emit(cg, "MOV C, D1");
    cg_emit(cg, "SHR DWORD C, 31");
    cg_emit(cg, "MOV D2, X");
    cg_emit(cg, "SHR DWORD D2, 31");
    cg_emit(cg, "SHL DWORD X, 1");
    cg_emit(cg, "OR DWORD X, C");
    cg_emit(cg, "SHL DWORD I, 1");
    cg_emit(cg, "OR DWORD I, D2");
    cg_emit(cg, "MOV D2, A");
    cg_emit(cg, "SHR DWORD D2, 31");
    cg_emit(cg, "SHL DWORD A, 1");
    cg_emit(cg, "SHL DWORD D1, 1");
    cg_emit(cg, "OR DWORD D1, D2");
    const char *lsk = cg_new_label(cg, "US");
    const char *lds = cg_new_label(cg, "US");
    cg_emit(cg, "MOV C, I");
    cg_emit(cg, "LET E, DWORD %s", lsk);
    cg_emit(cg, "JB DWORD R");
    cg_emit(cg, "LET E, DWORD %s", lds);
    cg_emit(cg, "JA DWORD R");
    cg_emit(cg, "MOV C, X");
    cg_emit(cg, "LET E, DWORD %s", lsk);
    cg_emit(cg, "JB DWORD B");
    cg_emit(cg, "%s:", lds);
    cg_emit(cg, "MOV C, X");
    cg_emit(cg, "LET E, DWORD %s_nb", lsk);
    cg_emit(cg, "JNB DWORD B");
    cg_emit(cg, "SUB DWORD I, 1");
    cg_emit(cg, "%s_nb:", lsk);
    cg_emit(cg, "SUB DWORD X, B");
    cg_emit(cg, "SUB DWORD I, R");
    cg_emit(cg, "LET C, DWORD 1");
    cg_emit(cg, "OR DWORD A, C");
    cg_emit(cg, "%s:", lsk);
    cg_emit(cg, "POP DWORD D2");
    cg_emit(cg, "DEC D2");
    cg_emit(cg, "PUSH DWORD D2");
    cg_emit(cg, "MOV C, D2");
    cg_emit(cg, "ZERO T");
    cg_emit(cg, "CMP DWORD T");
    cg_emit(cg, "LET E, DWORD %s", lp);
    cg_emit(cg, "JNZ");
    cg_emit(cg, "POP DWORD D2");
    cg_emit(cg, "MOV C, X");
    cg_emit(cg, "MOV D2, I");
    cg_emit(cg, "POP DWORD I");
    cg_emit(cg, "POP DWORD X");
}

static void emit_long_divmod(CodeGen *cg, int want_rem, int uns) {
    if (uns) {
        emit_long_udiv(cg);
        if (want_rem) {
            cg_emit(cg, "MOV A, C");
            cg_emit(cg, "MOV D1, D2");
        }
        return;
    }
    /* 有符号：记录符号 → 取绝对值 → 无符号核心 → 修正 */
    cg_emit(cg, "ZERO T");
    cg_emit(cg, "MOV C, D1");
    cg_emit(cg, "SHR DWORD C, 31");
    cg_emit(cg, "PUSH DWORD C");
    cg_emit(cg, "CMP DWORD T");
    const char *lp1 = cg_new_label(cg, "SD");
    cg_emit(cg, "LET E, DWORD %s", lp1);
    cg_emit(cg, "JZ");
    emit_long_neg(cg);
    cg_emit(cg, "%s:", lp1);
    cg_emit(cg, "ZERO T");
    cg_emit(cg, "MOV C, R");
    cg_emit(cg, "SHR DWORD C, 31");
    cg_emit(cg, "PUSH DWORD C");
    cg_emit(cg, "CMP DWORD T");
    const char *lp2 = cg_new_label(cg, "SD");
    cg_emit(cg, "LET E, DWORD %s", lp2);
    cg_emit(cg, "JZ");
    cg_emit(cg, "MOV A, B");
    cg_emit(cg, "MOV D1, R");
    emit_long_neg(cg);
    cg_emit(cg, "MOV B, A");
    cg_emit(cg, "MOV R, D1");
    cg_emit(cg, "%s:", lp2);
    emit_long_udiv(cg);
    cg_emit(cg, "POP DWORD B");
    cg_emit(cg, "POP DWORD R");
    cg_emit(cg, "PUSH DWORD R");
    cg_emit(cg, "PUSH DWORD C");
    cg_emit(cg, "PUSH DWORD D2");
    cg_emit(cg, "XOR DWORD R, B");
    cg_emit(cg, "ZERO T");
    cg_emit(cg, "MOV C, R");
    cg_emit(cg, "CMP DWORD T");
    const char *lq = cg_new_label(cg, "SQ");
    cg_emit(cg, "LET E, DWORD %s", lq);
    cg_emit(cg, "JZ");
    emit_long_neg(cg);
    cg_emit(cg, "%s:", lq);
    if (!want_rem) {
        cg_emit(cg, "POP DWORD D2");
        cg_emit(cg, "POP DWORD C");
        cg_emit(cg, "POP DWORD C");
    } else {
        cg_emit(cg, "POP DWORD D2");
        cg_emit(cg, "POP DWORD C");
        cg_emit(cg, "MOV A, C");
        cg_emit(cg, "MOV D1, D2");
        cg_emit(cg, "POP DWORD C");
        cg_emit(cg, "ZERO T");
        cg_emit(cg, "CMP DWORD T");
        const char *lr = cg_new_label(cg, "SR");
        cg_emit(cg, "LET E, DWORD %s", lr);
        cg_emit(cg, "JZ");
        emit_long_neg(cg);
        cg_emit(cg, "%s:", lr);
    }
}

static void emit_long_shift(CodeGen *cg, const char *op, int v, int uns) {
    if (v == 0) return;
    if (v >= 64) {
        if (strcmp(op,"<<")==0 || uns) {
            cg_emit(cg, "ZERO A");
            cg_emit(cg, "ZERO D1");
        } else {
            cg_emit(cg, "MOV A, D1");
            cg_emit(cg, "MSR DWORD A, 31");
            cg_emit(cg, "MOV D1, A");
        }
        return;
    }
    if (v >= 32) {
        if (strcmp(op,"<<")==0) {
            cg_emit(cg, "MOV C, A");
            cg_emit(cg, "SHL DWORD C, %d", v-32);
            cg_emit(cg, "ZERO A");
            cg_emit(cg, "MOV D1, C");
        } else {
            cg_emit(cg, "MOV A, D1");
            cg_emit(cg, (uns ? "SHR DWORD A, %d" : "MSR DWORD A, %d"), v-32);
            if (uns) cg_emit(cg, "ZERO D1");
            else cg_emit(cg, "MSR DWORD D1, 31");
        }
        return;
    }
    if (strcmp(op,"<<")==0) {
        cg_emit(cg, "MOV C, A");
        cg_emit(cg, "SHR DWORD C, %d", 32-v);
        cg_emit(cg, "SHL DWORD A, %d", v);
        cg_emit(cg, "SHL DWORD D1, %d", v);
        cg_emit(cg, "OR DWORD D1, C");
    } else {
        cg_emit(cg, "MOV C, D1");
        cg_emit(cg, "SHL DWORD C, %d", 32-v);
        cg_emit(cg, "SHR DWORD A, %d", v);
        cg_emit(cg, (uns ? "SHR DWORD D1, %d" : "MSR DWORD D1, %d"), v);
        cg_emit(cg, "OR DWORD A, C");
    }
}

static void gen_long_binop(CodeGen *cg, Expr *e) {
    const char *op = e->op;
    int uns = expr_unsigned(cg, e);
    /* 移位：右操作数必须是常量 */
    if (strcmp(op,"<<")==0 || strcmp(op,">>")==0) {
        if (e->r->kind != EXPR_NUM) {
            fprintf(stderr, "line %d: 64 位移位量必须是常量\n", e->line);
            exit(1);
        }
        gen_expr(cg, e->l);
        if (expr_size(cg, e->l) != 8) {
            if (expr_unsigned(cg, e->l)) cg_emit(cg, "ZERO D1");
            else cg_emit(cg, "MSR DWORD D1, 31");
        }
        emit_long_shift(cg, op, e->r->ival, expr_unsigned(cg, e->l));
        return;
    }
    /* 求右，提升到 64 位 */
    gen_expr(cg, e->r);
    if (expr_size(cg, e->r) != 8) {
        if (expr_unsigned(cg, e->r)) cg_emit(cg, "ZERO D1");
        else cg_emit(cg, "MSR DWORD D1, 31");
    }
    cg_emit(cg, "PUSH DWORD A");
    cg_emit(cg, "PUSH DWORD D1");
    gen_expr(cg, e->l);
    if (expr_size(cg, e->l) != 8) {
        if (expr_unsigned(cg, e->l)) cg_emit(cg, "ZERO D1");
        else cg_emit(cg, "MSR DWORD D1, 31");
    }
    cg_emit(cg, "POP DWORD R");
    cg_emit(cg, "POP DWORD B");
    if (strcmp(op,"+")==0) emit_long_add(cg);
    else if (strcmp(op,"-")==0) emit_long_sub(cg);
    else if (strcmp(op,"*")==0) emit_long_mul(cg);
    else if (strcmp(op,"/")==0) emit_long_divmod(cg, 0, uns);
    else if (strcmp(op,"%")==0) emit_long_divmod(cg, 1, uns);
    else if (strcmp(op,"&")==0) { cg_emit(cg, "AND DWORD A, B"); cg_emit(cg, "AND DWORD D1, R"); }
    else if (strcmp(op,"|")==0) { cg_emit(cg, "OR DWORD A, B"); cg_emit(cg, "OR DWORD D1, R"); }
    else if (strcmp(op,"^")==0) { cg_emit(cg, "XOR DWORD A, B"); cg_emit(cg, "XOR DWORD D1, R"); }
    else if (strcmp(op,"==")==0 || strcmp(op,"!=")==0 || strcmp(op,"<")==0 ||
             strcmp(op,"<=")==0 || strcmp(op,">")==0 || strcmp(op,">=")==0) {
        const char *lt = cg_new_label(cg, "LC");
        const char *le = cg_new_label(cg, "LC");
        /* 简化：用高字/低字比较生成 0/1 */
        /* 相等 */
        if (strcmp(op,"==")==0 || strcmp(op,"!=")==0) {
            cg_emit(cg, "MOV C, D1");
            cg_emit(cg, "CMP DWORD R");
            cg_emit(cg, "LET E, DWORD %s", strcmp(op,"==")==0 ? le : lt);
            cg_emit(cg, "JNZ");
            cg_emit(cg, "MOV C, A");
            cg_emit(cg, "CMP DWORD B");
            cg_emit(cg, "LET E, DWORD %s", strcmp(op,"==")==0 ? le : lt);
            cg_emit(cg, "JNZ");
        } else {
            cg_emit(cg, "MOV C, D1");
            if (strcmp(op,"<")==0 || strcmp(op,"<=")==0) {
                cg_emit(cg, "LET E, DWORD %s", lt);
                cg_emit(cg, (uns ? "JB DWORD R" : "JL DWORD R"));
                cg_emit(cg, "LET E, DWORD %s", le);
                cg_emit(cg, (uns ? "JA DWORD R" : "JG DWORD R"));
            } else {
                cg_emit(cg, "LET E, DWORD %s", lt);
                cg_emit(cg, (uns ? "JA DWORD R" : "JG DWORD R"));
                cg_emit(cg, "LET E, DWORD %s", le);
                cg_emit(cg, (uns ? "JB DWORD R" : "JL DWORD R"));
            }
            cg_emit(cg, "MOV C, A");
            if (strcmp(op,"<")==0) { cg_emit(cg, "LET E, DWORD %s", lt); cg_emit(cg, "JB DWORD B"); }
            else if (strcmp(op,"<=")==0) { cg_emit(cg, "LET E, DWORD %s", lt); cg_emit(cg, "JNB DWORD B"); }
            else if (strcmp(op,">")==0) { cg_emit(cg, "LET E, DWORD %s", lt); cg_emit(cg, "JA DWORD B"); }
            else { cg_emit(cg, "LET E, DWORD %s", lt); cg_emit(cg, "JNA DWORD B"); }
        }
        cg_emit(cg, "LET A, DWORD 0");
        cg_emit(cg, "LET E, DWORD %s", le);
        cg_emit(cg, "JMP");
        cg_emit(cg, "%s:", lt);
        cg_emit(cg, "LET A, DWORD 1");
        cg_emit(cg, "%s:", le);
        cg_emit(cg, "ZERO D1");
    } else {
        fprintf(stderr, "不支持的 64 位运算: %s\n", op);
        exit(1);
    }
}


static void emit_signed_div32(CodeGen *cg) {
    const char *l1 = cg_new_label(cg, "SD");
    const char *l2 = cg_new_label(cg, "SD");
    const char *l3 = cg_new_label(cg, "SD");
    const char *l4 = cg_new_label(cg, "SD");
    const char *l5 = cg_new_label(cg, "SD");
    const char *l6 = cg_new_label(cg, "SD");
    const char *l7 = cg_new_label(cg, "SD");
    const char *l8 = cg_new_label(cg, "SD");
    cg_emit(cg, "ZERO T");
    cg_emit(cg, "MOV C, A");
    cg_emit(cg, "CMP DWORD T");
    cg_emit(cg, "LET E, DWORD %s", l1);
    cg_emit(cg, "JNL DWORD T");
    cg_emit(cg, "LET X, DWORD 1");
    cg_emit(cg, "LET E, DWORD %s", l2);
    cg_emit(cg, "JMP");
    cg_emit(cg, "%s:", l1);
    cg_emit(cg, "ZERO X");
    cg_emit(cg, "%s:", l2);
    cg_emit(cg, "MOV C, B");
    cg_emit(cg, "CMP DWORD T");
    cg_emit(cg, "LET E, DWORD %s", l3);
    cg_emit(cg, "JNL DWORD T");
    cg_emit(cg, "LET R, DWORD 1");
    cg_emit(cg, "LET E, DWORD %s", l4);
    cg_emit(cg, "JMP");
    cg_emit(cg, "%s:", l3);
    cg_emit(cg, "ZERO R");
    cg_emit(cg, "%s:", l4);
    cg_emit(cg, "MOV I, X");
    cg_emit(cg, "XOR DWORD I, R");
    cg_emit(cg, "MOV C, A");
    cg_emit(cg, "CMP DWORD T");
    cg_emit(cg, "LET E, DWORD %s", l5);
    cg_emit(cg, "JNL DWORD T");
    cg_emit(cg, "MNE DWORD A");
    cg_emit(cg, "%s:", l5);
    cg_emit(cg, "MOV C, B");
    cg_emit(cg, "CMP DWORD T");
    cg_emit(cg, "LET E, DWORD %s", l6);
    cg_emit(cg, "JNL DWORD T");
    cg_emit(cg, "MNE DWORD B");
    cg_emit(cg, "%s:", l6);
    cg_emit(cg, "DIV DWORD A, B");
    cg_emit(cg, "MOV C, I");
    cg_emit(cg, "CMP DWORD T");
    cg_emit(cg, "LET E, DWORD %s", l7);
    cg_emit(cg, "JZ");
    cg_emit(cg, "MNE DWORD D2");
    cg_emit(cg, "%s:", l7);
    cg_emit(cg, "MOV C, X");
    cg_emit(cg, "CMP DWORD T");
    cg_emit(cg, "LET E, DWORD %s", l8);
    cg_emit(cg, "JZ");
    cg_emit(cg, "MNE DWORD D1");
    cg_emit(cg, "%s:", l8);
}

static void gen_binop(CodeGen *cg, Expr *e) {
    const char *op = e->op;
    if (strcmp(op, "&&") != 0 && strcmp(op, "||") != 0 && expr_size(cg, e) == 8) {
        gen_long_binop(cg, e);
        return;
    }
    if ((strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) && expr_size(cg, e) != 8) {
        if (e->r->kind != EXPR_NUM) {
            fprintf(stderr, "line %d: 移位量必须是常量\n", e->line);
            exit(1);
        }
        gen_expr(cg, e->l);
        if (strcmp(op, "<<") == 0)
            cg_emit(cg, "SHL DWORD A, %d", e->r->ival);
        else
            cg_emit(cg, (expr_unsigned(cg, e) ? "SHR DWORD A, %d" : "MSR DWORD A, %d"), e->r->ival);
        return;
    }
    if (strcmp(op, "&&") == 0) {
        const char *lf = cg_new_label(cg, "AND");
        const char *le = cg_new_label(cg, "AND");
        gen_expr(cg, e->l);
        cg_emit(cg, "MOV C, A");
        cg_emit(cg, "ZERO T");
        cg_emit(cg, "CMP DWORD T");
        cg_emit(cg, "LET E, DWORD %s", lf);
        cg_emit(cg, "JZ");
        gen_expr(cg, e->r);
        cg_emit(cg, "MOV C, A");
        cg_emit(cg, "CMP DWORD T");
        cg_emit(cg, "LET E, DWORD %s", lf);
        cg_emit(cg, "JZ");
        cg_emit(cg, "LET A, DWORD 1");
        cg_emit(cg, "LET E, DWORD %s", le);
        cg_emit(cg, "JMP");
        cg_emit(cg, "%s:", lf);
        cg_emit(cg, "LET A, DWORD 0");
        cg_emit(cg, "%s:", le);
        return;
    }
    if (strcmp(op, "||") == 0) {
        const char *lt = cg_new_label(cg, "OR");
        const char *le = cg_new_label(cg, "OR");
        gen_expr(cg, e->l);
        cg_emit(cg, "MOV C, A");
        cg_emit(cg, "ZERO T");
        cg_emit(cg, "CMP DWORD T");
        cg_emit(cg, "LET E, DWORD %s", lt);
        cg_emit(cg, "JNZ");
        gen_expr(cg, e->r);
        cg_emit(cg, "MOV C, A");
        cg_emit(cg, "CMP DWORD T");
        cg_emit(cg, "LET E, DWORD %s", lt);
        cg_emit(cg, "JNZ");
        cg_emit(cg, "LET A, DWORD 0");
        cg_emit(cg, "LET E, DWORD %s", le);
        cg_emit(cg, "JMP");
        cg_emit(cg, "%s:", lt);
        cg_emit(cg, "LET A, DWORD 1");
        cg_emit(cg, "%s:", le);
        return;
    }
    gen_expr(cg, e->r);
    cg_emit(cg, "PUSH DWORD A");
    gen_expr(cg, e->l);
    cg_emit(cg, "POP DWORD B");
    if (strcmp(op, "+")==0) cg_emit(cg, "ADD DWORD A, B");
    else if (strcmp(op, "-")==0) cg_emit(cg, "SUB DWORD A, B");
    else if (strcmp(op, "*")==0) { cg_emit(cg, "MUL DWORD A, B"); cg_emit(cg, "MOV A, D2"); }
    else if (strcmp(op, "/")==0) {
        if (expr_unsigned(cg, e)) { cg_emit(cg, "DIV DWORD A, B"); cg_emit(cg, "MOV A, D2"); }
        else { emit_signed_div32(cg); cg_emit(cg, "MOV A, D2"); }
    }
    else if (strcmp(op, "%")==0) {
        if (expr_unsigned(cg, e)) { cg_emit(cg, "DIV DWORD A, B"); cg_emit(cg, "MOV A, D1"); }
        else { emit_signed_div32(cg); cg_emit(cg, "MOV A, D1"); }
    }
    else if (strcmp(op, "&")==0) cg_emit(cg, "AND DWORD A, B");
    else if (strcmp(op, "|")==0) cg_emit(cg, "OR DWORD A, B");
    else if (strcmp(op, "^")==0) cg_emit(cg, "XOR DWORD A, B");
    else if (strcmp(op, "<<")==0) cg_emit(cg, "SHL DWORD A, B");
    else if (strcmp(op, ">>")==0) cg_emit(cg, (expr_unsigned(cg, e) ? "SHR DWORD A, B" : "MSR DWORD A, B"));
    else if (strcmp(op,"==")==0 || strcmp(op,"!=")==0 || strcmp(op,"<")==0 ||
             strcmp(op,"<=")==0 || strcmp(op,">")==0 || strcmp(op,">=")==0) {
        const char *lt = cg_new_label(cg, "CMP");
        const char *le = cg_new_label(cg, "CMP");
        cg_emit(cg, "MOV C, A");
        cg_emit(cg, "CMP DWORD B");
        cg_emit(cg, "ZERO T");
        cg_emit(cg, "LET E, DWORD %s", lt);
        if (strcmp(op,"==")==0) cg_emit(cg, "JZ");
        else if (strcmp(op,"!=")==0) cg_emit(cg, "JNZ");
        else if (strcmp(op,"<")==0) cg_emit(cg, (expr_unsigned(cg, e) ? "JB DWORD T" : "JL DWORD T"));
        else if (strcmp(op,"<=")==0) cg_emit(cg, (expr_unsigned(cg, e) ? "JNA DWORD T" : "JNG DWORD T"));
        else if (strcmp(op,">")==0) cg_emit(cg, (expr_unsigned(cg, e) ? "JA DWORD T" : "JG DWORD T"));
        else cg_emit(cg, (expr_unsigned(cg, e) ? "JNB DWORD T" : "JNL DWORD T"));
        cg_emit(cg, "LET A, DWORD 0");
        cg_emit(cg, "LET E, DWORD %s", le);
        cg_emit(cg, "JMP");
        cg_emit(cg, "%s:", lt);
        cg_emit(cg, "LET A, DWORD 1");
        cg_emit(cg, "%s:", le);
    } else {
        fprintf(stderr, "不支持的二元运算: %s\n", op);
        exit(1);
    }
}

static void gen_assign(CodeGen *cg, Expr *e) {
    gen_expr(cg, e->r);
    cg_emit(cg, "PUSH DWORD A");
    var_addr(cg, e->l);
    cg_emit(cg, "MOV B, A");
    cg_emit(cg, "POP DWORD A");
    if (strcmp(e->op, "=") == 0) {
        emit_store_to_b(cg, var_size(cg, e->l));
        return;
    }
    cg_emit(cg, "PUSH DWORD B");
    cg_emit(cg, "PUSH DWORD A");
    emit_load_from_b(cg, var_size(cg, e->l), var_unsigned(cg, e->l));
    cg_emit(cg, "MOV C, A");
    cg_emit(cg, "POP DWORD B");
    const char *op = e->op;
    if (strcmp(op, "+=")==0) cg_emit(cg, "ADD DWORD C, B");
    else if (strcmp(op, "-=")==0) cg_emit(cg, "SUB DWORD C, B");
    else if (strcmp(op, "*=")==0) { cg_emit(cg, "MUL DWORD C, B"); cg_emit(cg, "MOV C, D2"); }
    else if (strcmp(op, "/=")==0) { cg_emit(cg, "DIV DWORD C, B"); cg_emit(cg, "MOV C, D2"); }
    else if (strcmp(op, "%=")==0) { cg_emit(cg, "DIV DWORD C, B"); cg_emit(cg, "MOV C, D1"); }
    else if (strcmp(op, "&=")==0) cg_emit(cg, "AND DWORD C, B");
    else if (strcmp(op, "|=")==0) cg_emit(cg, "OR DWORD C, B");
    else if (strcmp(op, "^=")==0) cg_emit(cg, "XOR DWORD C, B");
    else if (strcmp(op, "<<=")==0) cg_emit(cg, "SHL DWORD C, B");
    else if (strcmp(op, ">>=")==0) cg_emit(cg, "SHR DWORD C, B");
    cg_emit(cg, "POP DWORD B");
    cg_emit(cg, "MOV A, C");
    emit_store_to_b(cg, var_size(cg, e->l));
}

static int expr_size(CodeGen *cg, Expr *e) {
    if (!e) return 4;
    switch (e->kind) {
        case EXPR_NUM: return e->type_size == 8 ? 8 : 4;
        case EXPR_VAR: return var_size(cg, e);
        case EXPR_BIN: {
            int l = expr_size(cg, e->l), r = expr_size(cg, e->r);
            return (l == 8 || r == 8) ? 8 : 4;
        }
        case EXPR_UNARY: return expr_size(cg, e->r);
        case EXPR_ASSIGN: return var_size(cg, e->l);
        case EXPR_INCDEC: return var_size(cg, e->r ? e->r : e->l);
        default: return 4;
    }
}

static int expr_unsigned(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_NUM: return e->is_unsigned;
        case EXPR_VAR: return var_unsigned(cg, e);
        case EXPR_BIN: {
            int l = expr_unsigned(cg, e->l), r = expr_unsigned(cg, e->r);
            int ls = expr_size(cg, e->l), rs = expr_size(cg, e->r);
            if (ls != rs) return ls > rs ? l : r;
            return l || r;
        }
        case EXPR_UNARY: return expr_unsigned(cg, e->r);
        case EXPR_ASSIGN: return var_unsigned(cg, e->l);
        default: return 0;
    }
}

static void emit_load_var(CodeGen *cg, Expr *e) {
    var_addr(cg, e);
    int sz = var_size(cg, e);
    if (sz == 8) {
        cg_emit(cg, "MOV R, A");
        cg_emit(cg, "LR DWORD A, *R");
        cg_emit(cg, "ADD DWORD R, 4");
        cg_emit(cg, "LR DWORD D1, *R");
    } else if (sz == 1) {
        cg_emit(cg, "LR BYTE A, *A");
        if (!var_unsigned(cg, e)) {
            cg_emit(cg, "SHL DWORD A, 24");
            cg_emit(cg, "MSR DWORD A, 24");
        }
    } else if (sz == 2) {
        cg_emit(cg, "LR WORD A, *A");
        if (!var_unsigned(cg, e)) {
            cg_emit(cg, "SHL DWORD A, 16");
            cg_emit(cg, "MSR DWORD A, 16");
        }
    } else {
        cg_emit(cg, "LR DWORD A, *A");
    }
}

static void emit_store_to_b(CodeGen *cg, int size) {
    if (size == 8) {
        cg_emit(cg, "ST DWORD *B, A");
        cg_emit(cg, "MOV R, B");
        cg_emit(cg, "ADD DWORD R, 4");
        cg_emit(cg, "ST DWORD *R, D1");
    } else if (size == 1) {
        cg_emit(cg, "ST BYTE *B, A");
    } else if (size == 2) {
        cg_emit(cg, "ST WORD *B, A");
    } else {
        cg_emit(cg, "ST DWORD *B, A");
    }
}

static void emit_load_from_b(CodeGen *cg, int size, int is_unsigned) {
    if (size == 8) {
        cg_emit(cg, "MOV R, B");
        cg_emit(cg, "LR DWORD A, *R");
        cg_emit(cg, "ADD DWORD R, 4");
        cg_emit(cg, "LR DWORD D1, *R");
    } else if (size == 1) {
        cg_emit(cg, "LR BYTE A, *B");
        if (!is_unsigned) {
            cg_emit(cg, "SHL DWORD A, 24");
            cg_emit(cg, "MSR DWORD A, 24");
        }
    } else if (size == 2) {
        cg_emit(cg, "LR WORD A, *B");
        if (!is_unsigned) {
            cg_emit(cg, "SHL DWORD A, 16");
            cg_emit(cg, "MSR DWORD A, 16");
        }
    } else {
        cg_emit(cg, "LR DWORD A, *B");
    }
}

static void gen_expr(CodeGen *cg, Expr *e) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_NUM:
            cg_emit(cg, "LET A, DWORD %lld", e->ival);
            if (e->type_size == 8) {
                cg_emit(cg, "LET D1, DWORD %lld", (long long)((unsigned long long)e->ival >> 32));
            }
            break;
        case EXPR_VAR:
            emit_load_var(cg, e);
            break;
        case EXPR_UNARY:
            gen_expr(cg, e->r);
            if (strcmp(e->op, "-")==0) cg_emit(cg, "MNE DWORD A");
            else if (strcmp(e->op, "~")==0) cg_emit(cg, "NEG A");
            else if (strcmp(e->op, "!")==0) {
                const char *l1 = cg_new_label(cg, "NOT");
                const char *l2 = cg_new_label(cg, "NOT");
                cg_emit(cg, "MOV C, A");
                cg_emit(cg, "ZERO T");
                cg_emit(cg, "CMP DWORD T");
                cg_emit(cg, "LET E, DWORD %s", l1);
                cg_emit(cg, "JNZ");
                cg_emit(cg, "LET A, DWORD 1");
                cg_emit(cg, "LET E, DWORD %s", l2);
                cg_emit(cg, "JMP");
                cg_emit(cg, "%s:", l1);
                cg_emit(cg, "LET A, DWORD 0");
                cg_emit(cg, "%s:", l2);
            }
            break;
        case EXPR_BIN:
            gen_binop(cg, e);
            break;
        case EXPR_ASSIGN:
            gen_assign(cg, e);
            break;
        case EXPR_CALL:
            gen_call(cg, e);
            break;
        case EXPR_INCDEC:
            gen_incdec(cg, e, e->postfix);
            break;
    }
}

static void gen_stmt(CodeGen *cg, Stmt *s) {
    if (!s) return;
    switch (s->kind) {
        case STMT_EMPTY:
            break;
        case STMT_EXPR:
            gen_expr(cg, s->expr);
            break;
        case STMT_DECL:
            if (s->expr) {
                gen_expr(cg, s->expr);
                cg_emit(cg, "PUSH DWORD A");
                Expr tmp;
                memset(&tmp, 0, sizeof(tmp));
                tmp.kind = EXPR_VAR;
                tmp.name = s->name;
                var_addr(cg, &tmp);
                cg_emit(cg, "MOV B, A");
                cg_emit(cg, "POP DWORD A");
                emit_store_to_b(cg, s->decl_size > 0 ? s->decl_size : 4);
            }
            break;
        case STMT_RETURN: {
            if (s->expr) {
                gen_expr(cg, s->expr);
                if (expr_size(cg, s->expr) != 8)
                    cg_emit(cg, "MOV D1, A");
            }
            cg_emit(cg, "RER");
            cg_emit(cg, "JMP");
            break;
        }
        case STMT_IF: {
            const char *lf = cg_new_label(cg, "IF");
            const char *le = cg_new_label(cg, "IE");
            gen_expr(cg, s->cond);
            cg_emit(cg, "MOV C, A");
            cg_emit(cg, "ZERO T");
            cg_emit(cg, "CMP DWORD T");
            cg_emit(cg, "LET E, DWORD %s", lf);
            cg_emit(cg, "JZ");
            gen_stmt(cg, s->then);
            cg_emit(cg, "LET E, DWORD %s", le);
            cg_emit(cg, "JMP");
            cg_emit(cg, "%s:", lf);
            if (s->els) gen_stmt(cg, s->els);
            cg_emit(cg, "%s:", le);
            break;
        }
        case STMT_WHILE: {
            const char *ls = cg_new_label(cg, "WL");
            const char *le = cg_new_label(cg, "WE");
            cg_emit(cg, "%s:", ls);
            gen_expr(cg, s->cond);
            cg_emit(cg, "MOV C, A");
            cg_emit(cg, "ZERO T");
            cg_emit(cg, "CMP DWORD T");
            cg_emit(cg, "LET E, DWORD %s", le);
            cg_emit(cg, "JZ");
            cg_push_loop(cg, le, ls);
            gen_stmt(cg, s->body);
            cg_pop_loop(cg);
            cg_emit(cg, "LET E, DWORD %s", ls);
            cg_emit(cg, "JMP");
            cg_emit(cg, "%s:", le);
            break;
        }
        case STMT_FOR: {
            if (s->init) gen_expr(cg, s->init);
            const char *ls = cg_new_label(cg, "FL");
            const char *le = cg_new_label(cg, "FE");
            const char *lc = cg_new_label(cg, "FC");
            cg_emit(cg, "%s:", ls);
            if (s->cond) {
                gen_expr(cg, s->cond);
                cg_emit(cg, "MOV C, A");
                cg_emit(cg, "ZERO T");
                cg_emit(cg, "CMP DWORD T");
                cg_emit(cg, "LET E, DWORD %s", le);
                cg_emit(cg, "JZ");
            }
            cg_push_loop(cg, le, lc);
            gen_stmt(cg, s->body);
            cg_pop_loop(cg);
            cg_emit(cg, "%s:", lc);
            if (s->inc) gen_expr(cg, s->inc);
            cg_emit(cg, "LET E, DWORD %s", ls);
            cg_emit(cg, "JMP");
            cg_emit(cg, "%s:", le);
            break;
        }
        case STMT_BREAK:
            if (cg->nloops > 0) {
                cg_emit(cg, "LET E, DWORD %s", cg->loops[cg->nloops-1].brk);
                cg_emit(cg, "JMP");
            }
            break;
        case STMT_CONTINUE:
            if (cg->nloops > 0) {
                cg_emit(cg, "LET E, DWORD %s", cg->loops[cg->nloops-1].cont);
                cg_emit(cg, "JMP");
            }
            break;
        case STMT_BLOCK:
            for (int i = 0; i < s->nitems; i++) gen_stmt(cg, s->items[i]);
            break;
    }
}

static void gen_func(CodeGen *cg, Function *f) {
    cg->nlocals = 0;
    cg->nparams = 0;
    int n = f->nparams;
    int acc = 0;
    for (int i = n - 1; i >= 0; i--) {
        int sz = (f->param_sizes && f->param_sizes[i] > 0) ? f->param_sizes[i] : 4;
        acc += sz;
        cg_push_param(cg, f->params[i], acc + 3, sz,
                      f->param_unsigned ? f->param_unsigned[i] : 0);
    }
    int frame = 4;
    collect_locals(cg, f->body, f->nbody, &frame);
    cg->frame_size = frame;
    cg_emit(cg, "func_%s:", f->name);
    cg_emit(cg, "SFA DWORD %d", frame);
    cg->nloops = 0;
    for (int i = 0; i < f->nbody; i++) gen_stmt(cg, f->body[i]);
    if (f->ret_void) {
        cg_emit(cg, "RER");
        cg_emit(cg, "JMP");
    } else {
        if (f->ret_size != 8)
            cg_emit(cg, "MOV D1, A");
        cg_emit(cg, "RER");
        cg_emit(cg, "JMP");
    }
}

int generate_code(Program *p, const char *outpath, char **err) {
    FILE *out = fopen(outpath, "w");
    if (!out) {
        if (err) *err = xstrdup("无法打开输出文件");
        return 0;
    }
    CodeGen cg;
    memset(&cg, 0, sizeof(cg));
    cg.out = out;
    cg.prog = p;
    fprintf(out, "\tSECTION DATA\n\tORG 0\n");
    int off = 0;
    for (int i = 0; i < p->nglobals; i++) {
        Global *g = &p->globals[i];
        fprintf(out, "var_%s:\n", g->name);
        int gsz = g->type_size > 0 ? g->type_size : 4;
        int val = g->has_init ? g->init : 0;
        if (gsz == 8) {
            fprintf(out, "\tDD %d, %d\n", off, val);
            fprintf(out, "\tDD %d, 0\n", off + 4);
        } else if (gsz == 1) {
            fprintf(out, "\tDB %d, 0x%02X\n", off, val & 0xff);
        } else if (gsz == 2) {
            fprintf(out, "\tDW %d, %d\n", off, val & 0xffff);
        } else {
            fprintf(out, "\tDD %d, %d\n", off, val);
        }
        off += gsz;
    }
    fprintf(out, "\n\tSECTION TEXT\n\tORG 0\n");
    for (int i = 0; i < p->nfuncs; i++) gen_func(&cg, &p->funcs[i]);
    fclose(out);
    if (err) *err = NULL;
    return 1;
}
