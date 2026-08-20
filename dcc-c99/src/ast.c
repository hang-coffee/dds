#include "ast.h"
#include <stdlib.h>
#include <string.h>

void expr_free(Expr *e) {
    if (!e) return;
    free(e->name);
    free(e->op);
    expr_free(e->l);
    expr_free(e->r);
    for (int i = 0; i < e->nargs; i++) expr_free(e->args[i]);
    free(e->args);
    free(e);
}

void stmt_free(Stmt *s) {
    if (!s) return;
    expr_free(s->expr);
    expr_free(s->cond);
    expr_free(s->init);
    expr_free(s->inc);
    stmt_free(s->then);
    stmt_free(s->els);
    stmt_free(s->body);
    free(s->name);
    for (int i = 0; i < s->nitems; i++) stmt_free(s->items[i]);
    free(s->items);
    free(s);
}

void program_free(Program *p) {
    for (int i = 0; i < p->nglobals; i++) free(p->globals[i].name);
    free(p->globals);
    for (int i = 0; i < p->nfuncs; i++) {
        Function *f = &p->funcs[i];
        free(f->name);
        for (int j = 0; j < f->nparams; j++) free(f->params[j]);
        free(f->params);
        free(f->param_sizes);
        free(f->param_unsigned);
        free(f->param_float);
        free(f->param_double);
        free(f->param_const);
        for (int j = 0; j < f->nbody; j++) stmt_free(f->body[j]);
        free(f->body);
    }
    free(p->funcs);
    memset(p, 0, sizeof(*p));
}
