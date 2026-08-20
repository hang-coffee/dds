#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    TokenArray *ta;
    int i;
    char err[512];
} Parser;

static Token *peek(Parser *p, int off) {
    int j = p->i + off;
    if (j >= p->ta->count) j = p->ta->count - 1;
    return &p->ta->toks[j];
}

static Token *next(Parser *p) {
    Token *t = &p->ta->toks[p->i];
    if (t->kind != T_EOF) p->i++;
    return t;
}

static int accept_op(Parser *p, const char *op) {
    if (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, op) == 0) {
        next(p);
        return 1;
    }
    return 0;
}

static Token *expect_op(Parser *p, const char *op) {
    Token *t = peek(p,0);
    if (t->kind != T_OP || strcmp(t->text, op) != 0) {
        snprintf(p->err, sizeof(p->err), "line %d: 期望 '%s'，得到 '%s'", t->line, op, t->text);
        return NULL;
    }
    return next(p);
}

static Token *expect_kind(Parser *p, TokenKind k) {
    Token *t = peek(p,0);
    if (t->kind != k) {
        snprintf(p->err, sizeof(p->err), "line %d: 期望标识符，得到 '%s'", t->line, t->text);
        return NULL;
    }
    return next(p);
}

static int is_type(Parser *p) {
    Token *t = peek(p,0);
    return t->kind == T_KW && (strcmp(t->text,"int")==0 || strcmp(t->text,"char")==0 || strcmp(t->text,"void")==0);
}

static Expr *expr_new(ExprKind k) {
    Expr *e = (Expr *)calloc(1, sizeof(Expr));
    e->kind = k;
    e->line = 0;
    return e;
}

static Stmt *stmt_new(StmtKind k) {
    Stmt *s = (Stmt *)calloc(1, sizeof(Stmt));
    s->kind = k;
    return s;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void push_stmt(Stmt ***arr, int *n, int *cap, Stmt *s) {
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 8;
        *arr = (Stmt **)realloc(*arr, (size_t)(*cap) * sizeof(Stmt *));
    }
    (*arr)[(*n)++] = s;
}

static void push_expr(Expr ***arr, int *n, int *cap, Expr *e) {
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 4;
        *arr = (Expr **)realloc(*arr, (size_t)(*cap) * sizeof(Expr *));
    }
    (*arr)[(*n)++] = e;
}

static Expr *parse_assign(Parser *p);
static Stmt *parse_stmt(Parser *p);

static Stmt **parse_block_until(Parser *p, const char *end, int *out_n) {
    Stmt **arr = NULL;
    int n = 0, cap = 0;
    while (peek(p,0)->kind != T_EOF && strcmp(peek(p,0)->text, end) != 0) {
        Stmt *s = parse_stmt(p);
        if (!s) { free(arr); return NULL; }
        push_stmt(&arr, &n, &cap, s);
    }
    *out_n = n;
    return arr;
}

static Expr *parse_primary(Parser *p) {
    Token *t = next(p);
    if (t->kind == T_NUM) {
        Expr *e = expr_new(EXPR_NUM);
        e->ival = t->ival;
        e->line = t->line;
        return e;
    }
    if (t->kind == T_ID) {
        Expr *e = expr_new(EXPR_VAR);
        e->name = xstrdup(t->text);
        e->line = t->line;
        return e;
    }
    if (t->kind == T_OP && strcmp(t->text, "(") == 0) {
        Expr *e = parse_assign(p);
        if (!e) return NULL;
        if (!expect_op(p, ")")) { expr_free(e); return NULL; }
        return e;
    }
    snprintf(p->err, sizeof(p->err), "line %d: 期望表达式，得到 '%s'", t->line, t->text);
    return NULL;
}

static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);
    if (!e) return NULL;
    while (1) {
        if (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "(") == 0) {
            next(p);
            Expr *call = expr_new(EXPR_CALL);
            call->name = e->name ? xstrdup(e->name) : NULL;
            call->line = e->line;
            if (!accept_op(p, ")")) {
                int acap = 0;
                while (1) {
                    Expr *a = parse_assign(p);
                    if (!a) { expr_free(e); expr_free(call); return NULL; }
                    push_expr(&call->args, &call->nargs, &acap, a);
                    if (!accept_op(p, ",")) break;
                }
                if (!expect_op(p, ")")) { expr_free(e); expr_free(call); return NULL; }
            }
            expr_free(e);
            e = call;
        } else if (accept_op(p, "++")) {
            Expr *n = expr_new(EXPR_INCDEC);
            n->op = xstrdup("+");
            n->postfix = 1;
            n->l = e;
            e = n;
        } else if (accept_op(p, "--")) {
            Expr *n = expr_new(EXPR_INCDEC);
            n->op = xstrdup("-");
            n->postfix = 1;
            n->l = e;
            e = n;
        } else {
            break;
        }
    }
    return e;
}

static Expr *parse_unary(Parser *p) {
    if (peek(p,0)->kind == T_OP && (strcmp(peek(p,0)->text,"-")==0 ||
        strcmp(peek(p,0)->text,"!")==0 || strcmp(peek(p,0)->text,"~")==0)) {
        Token *t = next(p);
        Expr *e = expr_new(EXPR_UNARY);
        e->op = xstrdup(t->text);
        e->line = t->line;
        e->r = parse_unary(p);
        return e;
    }
    if (accept_op(p, "++")) {
        Expr *e = expr_new(EXPR_INCDEC);
        e->op = xstrdup("+");
        e->postfix = 0;
        e->r = parse_unary(p);
        return e;
    }
    if (accept_op(p, "--")) {
        Expr *e = expr_new(EXPR_INCDEC);
        e->op = xstrdup("-");
        e->postfix = 0;
        e->r = parse_unary(p);
        return e;
    }
    return parse_postfix(p);
}

typedef struct { const char *op; int prec; } BinInfo;

static int bin_prec(const char *op) {
    static const BinInfo infos[] = {
        {"||",1},{"&&",2},{"|",3},{"^",4},{"&",5},
        {"==",6},{"!=",6},{"<",7},{"<=",7},{">",7},{">=",7},
        {"<<",8},{">>",8},{"+",9},{"-",9},{"*",10},{"/",10},{"%",10},
        {NULL,0}
    };
    for (int i = 0; infos[i].op; i++)
        if (strcmp(op, infos[i].op) == 0) return infos[i].prec;
    return 0;
}

static Expr *parse_binary(Parser *p, int min_prec) {
    Expr *left = parse_unary(p);
    if (!left) return NULL;
    while (1) {
        Token *t = peek(p,0);
        if (t->kind != T_OP) break;
        int prec = bin_prec(t->text);
        if (prec == 0 || prec < min_prec) break;
        next(p);
        Expr *right = parse_binary(p, prec + 1);
        if (!right) { expr_free(left); return NULL; }
        Expr *n = expr_new(EXPR_BIN);
        n->op = xstrdup(t->text);
        n->l = left;
        n->r = right;
        left = n;
    }
    return left;
}

static Expr *parse_cond(Parser *p) {
    return parse_binary(p, 1);
}

static Expr *parse_assign(Parser *p) {
    Expr *left = parse_cond(p);
    if (!left) return NULL;
    Token *t = peek(p,0);
    if (t->kind == T_OP && (strcmp(t->text,"=")==0 || strcmp(t->text,"+=")==0 ||
        strcmp(t->text,"-=")==0 || strcmp(t->text,"*=")==0 || strcmp(t->text,"/=")==0 ||
        strcmp(t->text,"%=")==0 || strcmp(t->text,"&=")==0 || strcmp(t->text,"|=")==0 ||
        strcmp(t->text,"^=")==0 || strcmp(t->text,"<<=")==0 || strcmp(t->text,">>=")==0)) {
        next(p);
        Expr *right = parse_assign(p);
        if (!right) { expr_free(left); return NULL; }
        Expr *n = expr_new(EXPR_ASSIGN);
        n->op = xstrdup(t->text);
        n->l = left;
        n->r = right;
        return n;
    }
    return left;
}

static Stmt *parse_stmt(Parser *p) {
    if (accept_op(p, "{")) {
        Stmt *s = stmt_new(STMT_BLOCK);
        int n = 0;
        s->items = parse_block_until(p, "}", &n);
        if (!s->items) { free(s); return NULL; }
        s->nitems = n;
        if (!expect_op(p, "}")) { stmt_free(s); return NULL; }
        return s;
    }
    if (accept_op(p, ";")) return stmt_new(STMT_EMPTY);
    if (peek(p,0)->kind == T_KW && (strcmp(peek(p,0)->text,"int")==0 || strcmp(peek(p,0)->text,"char")==0)) {
        next(p);
        Token *name = expect_kind(p, T_ID);
        if (!name) return NULL;
        Stmt *s = stmt_new(STMT_DECL);
        s->name = xstrdup(name->text);
        if (accept_op(p, "=")) s->expr = parse_assign(p);
        if (!expect_op(p, ";")) { stmt_free(s); return NULL; }
        return s;
    }
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text,"return")==0) {
        next(p);
        Stmt *s = stmt_new(STMT_RETURN);
        if (!accept_op(p, ";")) {
            s->expr = parse_assign(p);
            if (!s->expr || !expect_op(p, ";")) { stmt_free(s); return NULL; }
        }
        return s;
    }
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text,"if")==0) {
        next(p);
        Stmt *s = stmt_new(STMT_IF);
        if (!expect_op(p, "(")) { free(s); return NULL; }
        s->cond = parse_assign(p);
        if (!s->cond || !expect_op(p, ")")) { stmt_free(s); return NULL; }
        s->then = parse_stmt(p);
        if (!s->then) { stmt_free(s); return NULL; }
        if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text,"else")==0) {
            next(p);
            s->els = parse_stmt(p);
            if (!s->els) { stmt_free(s); return NULL; }
        }
        return s;
    }
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text,"while")==0) {
        next(p);
        Stmt *s = stmt_new(STMT_WHILE);
        if (!expect_op(p, "(")) { free(s); return NULL; }
        s->cond = parse_assign(p);
        if (!s->cond || !expect_op(p, ")")) { stmt_free(s); return NULL; }
        s->body = parse_stmt(p);
        if (!s->body) { stmt_free(s); return NULL; }
        return s;
    }
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text,"for")==0) {
        next(p);
        Stmt *s = stmt_new(STMT_FOR);
        if (!expect_op(p, "(")) { free(s); return NULL; }
        if (!accept_op(p, ";")) { s->init = parse_assign(p); if (!s->init || !expect_op(p, ";")) { stmt_free(s); return NULL; } }
        if (!accept_op(p, ";")) { s->cond = parse_assign(p); if (!s->cond || !expect_op(p, ";")) { stmt_free(s); return NULL; } }
        if (!accept_op(p, ")")) { s->inc = parse_assign(p); if (!s->inc || !expect_op(p, ")")) { stmt_free(s); return NULL; } }
        s->body = parse_stmt(p);
        if (!s->body) { stmt_free(s); return NULL; }
        return s;
    }
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text,"break")==0) {
        next(p);
        if (!expect_op(p, ";")) return NULL;
        return stmt_new(STMT_BREAK);
    }
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text,"continue")==0) {
        next(p);
        if (!expect_op(p, ";")) return NULL;
        return stmt_new(STMT_CONTINUE);
    }
    Stmt *s = stmt_new(STMT_EXPR);
    s->expr = parse_assign(p);
    if (!s->expr || !expect_op(p, ";")) { stmt_free(s); return NULL; }
    return s;
}

Program parse_program(TokenArray *ta, char **err) {
    Program prog;
    memset(&prog, 0, sizeof(prog));
    Parser p;
    memset(&p, 0, sizeof(p));
    p.ta = ta;
    p.i = 0;

    while (peek(&p,0)->kind != T_EOF) {
        if (!is_type(&p)) {
            snprintf(p.err, sizeof(p.err), "line %d: 期望类型", peek(&p,0)->line);
            break;
        }
        Token *ty = next(&p);
        Token *name = expect_kind(&p, T_ID);
        if (!name) break;

        if (peek(&p,0)->kind == T_OP && strcmp(peek(&p,0)->text, "(") == 0) {
            next(&p);
            Function f;
            memset(&f, 0, sizeof(f));
            f.name = xstrdup(name->text);
            f.ret_void = strcmp(ty->text, "void") == 0;
            if (!accept_op(&p, ")")) {
                if (peek(&p,0)->kind == T_KW && strcmp(peek(&p,0)->text,"void")==0 &&
                    peek(&p,1)->kind == T_OP && strcmp(peek(&p,1)->text,")")==0) {
                    next(&p);
                    expect_op(&p, ")");
                } else {
                    while (1) {
                        if (!is_type(&p) || strcmp(peek(&p,0)->text,"void")==0) {
                            snprintf(p.err, sizeof(p.err), "line %d: 期望参数类型", peek(&p,0)->line);
                            break;
                        }
                        next(&p);
                        Token *pn = expect_kind(&p, T_ID);
                        if (!pn) break;
                        f.params = (char **)realloc(f.params, (size_t)(f.nparams+1)*sizeof(char *));
                        f.params[f.nparams++] = xstrdup(pn->text);
                        if (!accept_op(&p, ",")) break;
                    }
                    if (p.err[0] || !expect_op(&p, ")")) break;
                }
            }
            if (!expect_op(&p, "{")) break;
            int nbody = 0;
            f.body = parse_block_until(&p, "}", &nbody);
            if (!f.body) break;
            f.nbody = nbody;
            if (!expect_op(&p, "}")) break;
            prog.funcs = (Function *)realloc(prog.funcs, (size_t)(prog.nfuncs+1)*sizeof(Function));
            prog.funcs[prog.nfuncs++] = f;
        } else {
            Global g;
            memset(&g, 0, sizeof(g));
            g.name = xstrdup(name->text);
            g.has_init = 0;
            g.init = 0;
            if (accept_op(&p, "=")) {
                Token *v = next(&p);
                if (v->kind != T_NUM) {
                    snprintf(p.err, sizeof(p.err), "line %d: 全局初始化器必须是常量", v->line);
                    break;
                }
                g.has_init = 1;
                g.init = v->ival;
            }
            if (!expect_op(&p, ";")) break;
            prog.globals = (Global *)realloc(prog.globals, (size_t)(prog.nglobals+1)*sizeof(Global));
            prog.globals[prog.nglobals++] = g;
        }
    }

    if (p.err[0]) {
        *err = xstrdup(p.err);
        program_free(&prog);
        return prog;
    }
    *err = NULL;
    return prog;
}

void parse_error_free(char *err) {
    free(err);
}
