#include "ast.h"
#include <stdlib.h>
#include <string.h>

void expr_free(Expr *e) {
    if (!e) return;
    free(e->name);
    free(e->member);
    free(e->op);
    expr_free(e->l);
    expr_free(e->r);
    expr_free(e->c);
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
    free(s->asm_text);
    free(s->str_init);
    free(s->decl_struct_name);
    free(s->decl_func_ret_struct_name);
    free(s->static_label);
    for (int i = 0; i < s->n_init_list; i++) expr_free(s->init_list[i]);
    free(s->init_list);
    free(s->decl_dims);
    for (int i = 0; i < s->nitems; i++) stmt_free(s->items[i]);
    free(s->items);
    free(s);
}

void program_free(Program *p) {
    for (int i = 0; i < p->nglobals; i++) {
        free(p->globals[i].name);
        free(p->globals[i].dims);
        free(p->globals[i].str_init);
        free(p->globals[i].struct_name);
        free(p->globals[i].init_list);
        free(p->globals[i].func_ret_struct_name);
    }
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
        free(f->param_bool);
        free(f->param_ptr_depth);
        free(f->param_elem_size);
        free(f->param_base_size);
        free(f->param_is_struct);
        free(f->param_is_func_ptr);
        free(f->param_func_ret_size);
        free(f->param_func_ret_float);
        free(f->param_func_ret_double);
        free(f->param_func_ret_void);
        free(f->param_func_ret_is_struct);
        if (f->param_func_ret_struct_name) {
            for (int j = 0; j < f->nparams; j++) free(f->param_func_ret_struct_name[j]);
            free(f->param_func_ret_struct_name);
        }
        if (f->param_struct_name) {
            for (int j = 0; j < f->nparams; j++) free(f->param_struct_name[j]);
            free(f->param_struct_name);
        }
        free(f->ret_struct_name);
        for (int j = 0; j < f->nbody; j++) stmt_free(f->body[j]);
        free(f->body);
    }
    free(p->funcs);
    for (int i = 0; i < p->nstructs; i++) {
        free(p->structs[i].name);
        for (int j = 0; j < p->structs[i].nmembers; j++) {
            free(p->structs[i].members[j].name);
            free(p->structs[i].members[j].struct_name);
        }
        free(p->structs[i].members);
    }
    free(p->structs);
    memset(p, 0, sizeof(*p));
}
