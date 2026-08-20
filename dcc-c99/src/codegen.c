#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    char *name;
    int offset;
} VarInfo;

typedef struct {
    char *name;
    int offset;
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

static const char *cg_new_label(CodeGen *cg, const char *prefix) {
    static char buf[64];
    cg->label++;
    snprintf(buf, sizeof(buf), "%s%d", prefix, cg->label);
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

static void cg_push_local(CodeGen *cg, const char *name, int offset) {
    if (cg->nlocals >= cg->caplocals) {
        cg->caplocals = cg->caplocals ? cg->caplocals * 2 : 16;
        cg->locals = (VarInfo *)realloc(cg->locals, (size_t)cg->caplocals * sizeof(VarInfo));
    }
    cg->locals[cg->nlocals].name = xstrdup(name);
    cg->locals[cg->nlocals].offset = offset;
    cg->nlocals++;
}

static int local_offset(CodeGen *cg, const char *name) {
    for (int i = 0; i < cg->nlocals; i++)
        if (strcmp(cg->locals[i].name, name) == 0) return cg->locals[i].offset;
    return -1;
}

static void cg_push_param(CodeGen *cg, const char *name, int offset) {
    if (cg->nparams >= cg->capparams) {
        cg->capparams = cg->capparams ? cg->capparams * 2 : 8;
        cg->params = (ParamInfo *)realloc(cg->params, (size_t)cg->capparams * sizeof(ParamInfo));
    }
    cg->params[cg->nparams].name = xstrdup(name);
    cg->params[cg->nparams].offset = offset;
    cg->nparams++;
}

static int param_offset(CodeGen *cg, const char *name) {
    for (int i = 0; i < cg->nparams; i++)
        if (strcmp(cg->params[i].name, name) == 0) return cg->params[i].offset;
    return -1;
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
            cg_push_local(cg, s->name, *frame);
            *frame += 4;
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
    cg_emit(cg, "LR DWORD A, *B");
    if (postfix) cg_emit(cg, "PUSH DWORD A");
    if (strcmp(e->op, "+") == 0) cg_emit(cg, "ADD DWORD A, 1");
    else cg_emit(cg, "SUB DWORD A, 1");
    cg_emit(cg, "ST DWORD *B, A");
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
        cg_emit(cg, "PUSH DWORD A");
    }
    const char *ret = cg_new_label(cg, "RET");
    cg_emit(cg, "LET E, DWORD %s", ret);
    cg_emit(cg, "PUSH DWORD E");
    cg_emit(cg, "LET E, DWORD func_%s", e->name);
    cg_emit(cg, "JMP");
    cg_emit(cg, "%s:", ret);
    if (argbytes) cg_emit(cg, "SUB DWORD S, %d", argbytes);
    cg_emit(cg, "POP DWORD F");
    cg_emit(cg, "MOV A, D1");
}

static void gen_binop(CodeGen *cg, Expr *e) {
    const char *op = e->op;
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
    else if (strcmp(op, "/")==0) { cg_emit(cg, "DIV DWORD A, B"); cg_emit(cg, "MOV A, D2"); }
    else if (strcmp(op, "%")==0) { cg_emit(cg, "DIV DWORD A, B"); cg_emit(cg, "MOV A, D1"); }
    else if (strcmp(op, "&")==0) cg_emit(cg, "AND DWORD A, B");
    else if (strcmp(op, "|")==0) cg_emit(cg, "OR DWORD A, B");
    else if (strcmp(op, "^")==0) cg_emit(cg, "XOR DWORD A, B");
    else if (strcmp(op, "<<")==0) cg_emit(cg, "SHL DWORD A, B");
    else if (strcmp(op, ">>")==0) cg_emit(cg, "SHR DWORD A, B");
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
        else if (strcmp(op,"<")==0) cg_emit(cg, "JL DWORD T");
        else if (strcmp(op,"<=")==0) cg_emit(cg, "JNG DWORD T");
        else if (strcmp(op,">")==0) cg_emit(cg, "JG DWORD T");
        else cg_emit(cg, "JNL DWORD T");
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
        cg_emit(cg, "ST DWORD *B, A");
        return;
    }
    cg_emit(cg, "PUSH DWORD B");
    cg_emit(cg, "PUSH DWORD A");
    cg_emit(cg, "LR DWORD A, *B");
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
    cg_emit(cg, "ST DWORD *B, A");
}

static void gen_expr(CodeGen *cg, Expr *e) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_NUM:
            cg_emit(cg, "LET A, DWORD %d", e->ival);
            break;
        case EXPR_VAR:
            var_addr(cg, e);
            cg_emit(cg, "LR DWORD A, *A");
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
                Expr tmp;
                memset(&tmp, 0, sizeof(tmp));
                tmp.kind = EXPR_VAR;
                tmp.name = s->name;
                var_addr(cg, &tmp);
                cg_emit(cg, "MOV B, A");
                cg_emit(cg, "ST DWORD *B, A");
            }
            break;
        case STMT_RETURN: {
            if (s->expr) {
                gen_expr(cg, s->expr);
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
    for (int i = 0; i < n; i++) {
        cg_push_param(cg, f->params[i], 3 + 4 * (n - i));
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
        fprintf(out, "\tDD %d, %d\n", off, g->has_init ? g->init : 0);
        off += 4;
    }
    fprintf(out, "\n\tSECTION TEXT\n\tORG 0\n");
    for (int i = 0; i < p->nfuncs; i++) gen_func(&cg, &p->funcs[i]);
    fclose(out);
    if (err) *err = NULL;
    return 1;
}
