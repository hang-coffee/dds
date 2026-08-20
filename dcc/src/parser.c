#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TypeInfo TypeInfo;

typedef struct {
    TokenArray *ta;
    int i;
    char err[512];
    StructDef *structs;
    int nstructs, capstructs;
    TypeInfo *typedefs;
    int ntypedefs, captypedefs;
    char **enum_names;
    long long *enum_values;
    int nenums, capenums;
} Parser;

static char *xstrdup(const char *s);

typedef struct TypeInfo {
    int is_void;
    int size;
    int is_unsigned;
    int is_const;
    int is_float;
    int is_double;
    int is_bool;
    int is_volatile;
    int is_restrict;
    int ptr_depth;
    int is_array;
    int ndims;
    int *dims;
    int array_len;
    int is_struct;
    int is_union;
    int is_enum;
    int is_func_ptr;
    int func_ret_size;
    int func_ret_float;
    int func_ret_double;
    int func_ret_void;
    int func_ret_is_struct;
    char *func_ret_struct_name;
    char *name;          /* typedef 名（仅 typedef 表中使用） */
    char *struct_name;
    char *enum_name;
} TypeInfo;

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

static TypeInfo *parser_find_typedef(Parser *p, const char *name);

static int is_type(Parser *p) {
    Token *t = peek(p,0);
    if (t->kind == T_ID && parser_find_typedef(p, t->text)) return 1;
    if (t->kind != T_KW) return 0;
    return strcmp(t->text,"int")==0 || strcmp(t->text,"char")==0 || strcmp(t->text,"void")==0 ||
           strcmp(t->text,"short")==0 || strcmp(t->text,"long")==0 ||
           strcmp(t->text,"unsigned")==0 || strcmp(t->text,"signed")==0 ||
           strcmp(t->text,"const")==0 || strcmp(t->text,"float")==0 ||
           strcmp(t->text,"double")==0 || strcmp(t->text,"_Bool")==0 ||
           strcmp(t->text,"volatile")==0 || strcmp(t->text,"restrict")==0 ||
           strcmp(t->text,"struct")==0 || strcmp(t->text,"union")==0 ||
           strcmp(t->text,"enum")==0;
}

static StructDef *parser_find_struct(Parser *p, const char *name) {
    for (int i = 0; i < p->nstructs; i++)
        if (strcmp(p->structs[i].name, name) == 0) return &p->structs[i];
    return NULL;
}

static StructDef *parser_add_struct(Parser *p, const char *name) {
    if (p->nstructs >= p->capstructs) {
        p->capstructs = p->capstructs ? p->capstructs * 2 : 8;
        p->structs = (StructDef *)realloc(p->structs, (size_t)p->capstructs * sizeof(StructDef));
    }
    StructDef *d = &p->structs[p->nstructs++];
    memset(d, 0, sizeof(*d));
    d->name = xstrdup(name);
    return d;
}

static TypeInfo *parser_find_typedef(Parser *p, const char *name) {
    for (int i = 0; i < p->ntypedefs; i++)
        if (strcmp(p->typedefs[i].name ? p->typedefs[i].name : "", name) == 0) return &p->typedefs[i];
    return NULL;
}

static void parser_add_typedef(Parser *p, const char *name, TypeInfo *t) {
    if (p->ntypedefs >= p->captypedefs) {
        p->captypedefs = p->captypedefs ? p->captypedefs * 2 : 8;
        p->typedefs = (TypeInfo *)realloc(p->typedefs, (size_t)p->captypedefs * sizeof(TypeInfo));
    }
    TypeInfo *d = &p->typedefs[p->ntypedefs++];
    *d = *t;
    d->name = xstrdup(name);
    d->dims = NULL;
    d->struct_name = NULL;
    d->enum_name = NULL;
    d->func_ret_struct_name = NULL;
    if (t->dims) {
        d->dims = (int *)malloc((size_t)t->ndims * sizeof(int));
        memcpy(d->dims, t->dims, (size_t)t->ndims * sizeof(int));
    }
    if (t->struct_name) d->struct_name = xstrdup(t->struct_name);
    if (t->enum_name) d->enum_name = xstrdup(t->enum_name);
    if (t->func_ret_struct_name) d->func_ret_struct_name = xstrdup(t->func_ret_struct_name);
}

static int parser_find_enum(Parser *p, const char *name) {
    for (int i = 0; i < p->nenums; i++)
        if (strcmp(p->enum_names[i], name) == 0) return i;
    return -1;
}

static void parser_add_enum(Parser *p, const char *name, long long value) {
    int idx = parser_find_enum(p, name);
    if (idx >= 0) {
        p->enum_values[idx] = value;
        return;
    }
    if (p->nenums >= p->capenums) {
        p->capenums = p->capenums ? p->capenums * 2 : 16;
        p->enum_names = (char **)realloc(p->enum_names, (size_t)p->capenums * sizeof(char *));
        p->enum_values = (long long *)realloc(p->enum_values, (size_t)p->capenums * sizeof(long long));
    }
    p->enum_names[p->nenums] = xstrdup(name);
    p->enum_values[p->nenums] = value;
    p->nenums++;
}

static TypeInfo type_info_clone(const TypeInfo *src) {
    TypeInfo t = *src;
    t.name = NULL;
    t.dims = NULL;
    t.struct_name = NULL;
    t.enum_name = NULL;
    t.func_ret_struct_name = NULL;
    if (src->dims) {
        t.dims = (int *)malloc((size_t)src->ndims * sizeof(int));
        memcpy(t.dims, src->dims, (size_t)src->ndims * sizeof(int));
    }
    if (src->struct_name) t.struct_name = xstrdup(src->struct_name);
    if (src->enum_name) t.enum_name = xstrdup(src->enum_name);
    if (src->func_ret_struct_name) t.func_ret_struct_name = xstrdup(src->func_ret_struct_name);
    return t;
}

static TypeInfo parse_type_spec(Parser *p) {
    TypeInfo t;
    memset(&t, 0, sizeof(t));
    t.size = 4;
    int saw_long = 0;
    if (peek(p,0)->kind == T_ID) {
        TypeInfo *td = parser_find_typedef(p, peek(p,0)->text);
        if (td) {
            next(p);
            return type_info_clone(td);
        }
    }
    while (is_type(p)) {
        Token *tok = peek(p,0);
        if (strcmp(tok->text,"const")==0) { t.is_const=1; next(p); }
        else if (strcmp(tok->text,"signed")==0) { t.is_unsigned=0; next(p); }
        else if (strcmp(tok->text,"volatile")==0) { t.is_volatile=1; next(p); }
        else if (strcmp(tok->text,"restrict")==0) { t.is_restrict=1; next(p); }
        else if (strcmp(tok->text,"unsigned")==0) { t.is_unsigned=1; next(p); }
        else if (strcmp(tok->text,"short")==0) { t.size=2; next(p); }
        else if (strcmp(tok->text,"long")==0) { saw_long++; next(p); }
        else if (strcmp(tok->text,"char")==0) { t.size=1; next(p); break; }
        else if (strcmp(tok->text,"int")==0) { t.size=4; next(p); break; }
        else if (strcmp(tok->text,"float")==0) { t.size=4; t.is_float=1; next(p); break; }
        else if (strcmp(tok->text,"_Bool")==0) { t.size=1; t.is_bool=1; t.is_unsigned=1; next(p); break; }
        else if (strcmp(tok->text,"double")==0) { t.size=8; t.is_double=1; next(p); break; }
        else if (strcmp(tok->text,"void")==0) { t.is_void=1; t.size=0; next(p); break; }
        else if (strcmp(tok->text,"struct")==0 || strcmp(tok->text,"union")==0) {
            int is_union = strcmp(tok->text,"union") == 0;
            next(p);
            t.is_struct = !is_union;
            t.is_union = is_union;
            if (peek(p,0)->kind == T_ID) {
                t.struct_name = xstrdup(peek(p,0)->text);
                next(p);
            }
            StructDef *d = t.struct_name ? parser_find_struct(p, t.struct_name) : NULL;
            t.size = d ? d->size : 4;
            break;
        }
        else if (strcmp(tok->text,"enum")==0) {
            next(p);
            t.is_enum = 1;
            t.is_unsigned = 1;
            if (peek(p,0)->kind == T_ID) {
                t.enum_name = xstrdup(peek(p,0)->text);
                next(p);
            }
            t.size = 4;
            break;
        }
        else break;
    }
    if (saw_long) t.size = 8;
    return t;
}

static char *xstrdup(const char *s);

static int parse_array_suffix(Parser *p, TypeInfo *t) {
    while (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "[") == 0) {
        next(p);
        t->is_array = 1;
        if (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "]") == 0) {
            /* 允许空方括号：主要用于数组参数退化（int a[]） */
            next(p);
            continue;
        }
        Token *n = peek(p,0);
        if (n->kind != T_NUM || n->ival <= 0) {
            snprintf(p->err, sizeof(p->err), "line %d: 数组长度必须为正整数常量", n->line);
            return 0;
        }
        next(p);
        if (!expect_op(p, "]")) return 0;
        t->ndims++;
        t->dims = (int *)realloc(t->dims, (size_t)t->ndims * sizeof(int));
        t->dims[t->ndims - 1] = (int)n->ival;
        t->array_len = t->array_len == 0 ? (int)n->ival : t->array_len * (int)n->ival;
    }
    return 1;
}

static int parse_declarator(Parser *p, TypeInfo *t, char **out_name) {
    while (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "*") == 0) {
        next(p);
        t->ptr_depth++;
    }
    /* 函数指针：int (*fp)(int, int) */
    if (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "(") == 0 &&
        peek(p,1)->kind == T_OP && strcmp(peek(p,1)->text, "*") == 0) {
        next(p); /* ( */
        next(p); /* * */
        Token *name = expect_kind(p, T_ID);
        if (!name) return 0;
        *out_name = xstrdup(name->text);
        if (!expect_op(p, ")")) return 0;
        t->is_func_ptr = 1;
        t->func_ret_size = t->size;
        t->func_ret_float = t->is_float;
        t->func_ret_double = t->is_double;
        t->func_ret_void = t->is_void;
        t->func_ret_is_struct = t->is_struct || t->is_union;
        if (t->struct_name) {
            t->func_ret_struct_name = xstrdup(t->struct_name);
            free(t->struct_name);
            t->struct_name = NULL;
        }
        t->is_struct = 0;
        t->is_union = 0;
        t->is_void = 0;
        t->ptr_depth = 1;
        t->size = 4;
        /* 跳过参数列表 */
        if (!expect_op(p, "(")) return 0;
        int depth = 1;
        while (depth > 0 && peek(p,0)->kind != T_EOF) {
            Token *tok = next(p);
            if (tok->kind == T_OP && strcmp(tok->text, "(") == 0) depth++;
            else if (tok->kind == T_OP && strcmp(tok->text, ")") == 0) depth--;
        }
        if (depth != 0) return 0;
        return 1;
    }
    Token *name = expect_kind(p, T_ID);
    if (!name) return 0;
    *out_name = xstrdup(name->text);
    if (!parse_array_suffix(p, t)) return 0;
    return 1;
}

static void compute_type_sizes(TypeInfo *t, int *storage_size, int *elem_size, int *base_size) {
    int bs = t->size > 0 ? t->size : 4;
    int esz;
    if (t->ptr_depth > 0) {
        esz = t->ptr_depth > 1 ? 4 : bs;
        *storage_size = 4;
    } else if (t->is_array) {
        int total = bs;
        for (int i = 0; i < t->ndims; i++) total *= t->dims[i];
        *storage_size = total;
        esz = t->ndims > 1 ? (total / t->dims[0]) : bs;
    } else {
        esz = bs;
        *storage_size = bs;
    }
    *elem_size = esz;
    *base_size = bs;
}

static int parse_struct_body(Parser *p, TypeInfo *ty) {
    if (!expect_op(p, "{")) return 0;
    if (!ty->struct_name) {
        static int anon_id = 0;
        char buf[32];
        snprintf(buf, sizeof(buf), "__anon%d", anon_id++);
        ty->struct_name = xstrdup(buf);
    }
    if (parser_find_struct(p, ty->struct_name)) {
        snprintf(p->err, sizeof(p->err), "结构体/联合体 %s 重复定义", ty->struct_name);
        return 0;
    }
    StructDef *def = parser_add_struct(p, ty->struct_name);
    def->is_union = ty->is_union;
    int off = 0;
    int max_sz = 0;
    while (peek(p,0)->kind != T_EOF && strcmp(peek(p,0)->text, "}") != 0) {
        if (accept_op(p, ";")) continue;
        TypeInfo mt = parse_type_spec(p);
        char *mname = NULL;
        if (!parse_declarator(p, &mt, &mname)) { free(mt.dims); free(mt.struct_name); free(mt.enum_name); free(mname); return 0; }
        int st, es, bs;
        compute_type_sizes(&mt, &st, &es, &bs);
        def->members = (MemberDef *)realloc(def->members, (size_t)(def->nmembers + 1) * sizeof(MemberDef));
        MemberDef *md = &def->members[def->nmembers++];
        memset(md, 0, sizeof(*md));
        md->name = mname;
        md->size = st;
        md->elem_size = es;
        md->base_size = bs;
        md->is_unsigned = mt.is_unsigned;
        md->is_float = mt.is_float;
        md->is_double = mt.is_double;
        md->is_bool = mt.is_bool;
        md->is_struct = mt.is_struct;
        md->is_array = mt.is_array;
        md->array_len = mt.array_len;
        md->ptr_depth = mt.ptr_depth;
        if (mt.struct_name) {
            md->struct_name = xstrdup(mt.struct_name);
            free(mt.struct_name);
        }
        if (ty->is_union) {
            md->offset = 0;
            if (st > max_sz) max_sz = st;
        } else {
            md->offset = off;
            off += st;
        }
        free(mt.dims);
        free(mt.enum_name);
        if (!expect_op(p, ";")) return 0;
    }
    if (!expect_op(p, "}")) return 0;
    def->size = ty->is_union ? max_sz : off;
    ty->size = def->size;
    return 1;
}

static int parse_struct_definition(Parser *p, TypeInfo *ty) {
    if (!parse_struct_body(p, ty)) return 0;
    if (!expect_op(p, ";")) return 0;
    return 1;
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
static Expr *parse_expr(Parser *p);
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
    if (t->kind == T_FLOAT) {
        Expr *e = expr_new(EXPR_NUM);
        e->ival = (long long)t->fval;
        e->is_float = !t->is_double;
        e->is_double = t->is_double;
        e->type_size = t->is_double ? 8 : 4;
        e->line = t->line;
        e->fval = t->fval;
        return e;
    }
    if (t->kind == T_NUM) {
        Expr *e = expr_new(EXPR_NUM);
        e->ival = t->ival;
        e->type_size = t->is_long ? 8 : 4;
        e->is_unsigned = t->is_unsigned;
        e->line = t->line;
        return e;
    }
    if (t->kind == T_STR) {
        Expr *e = expr_new(EXPR_STR);
        e->name = xstrdup(t->text);
        while (peek(p,0)->kind == T_STR) {
            Token *n = next(p);
            size_t old = strlen(e->name);
            size_t add = strlen(n->text);
            e->name = (char *)realloc(e->name, old + add + 1);
            memcpy(e->name + old, n->text, add + 1);
        }
        e->type_size = 4;
        e->is_unsigned = 1;
        e->line = t->line;
        return e;
    }
    if (t->kind == T_ID) {
        int eidx = parser_find_enum(p, t->text);
        if (eidx >= 0) {
            Expr *e = expr_new(EXPR_NUM);
            e->ival = p->enum_values[eidx];
            e->type_size = 4;
            e->line = t->line;
            return e;
        }
        if (strncmp(t->text, "__reg_", 6) == 0) {
            const char *reg = t->text + 6;
            int ok = strcmp(reg, "A") == 0 || strcmp(reg, "B") == 0 ||
                     strcmp(reg, "C") == 0 || strcmp(reg, "D1") == 0 ||
                     strcmp(reg, "D2") == 0 || strcmp(reg, "X") == 0 ||
                     strcmp(reg, "I") == 0 || strcmp(reg, "S") == 0 ||
                     strcmp(reg, "R") == 0 || strcmp(reg, "F") == 0 ||
                     strcmp(reg, "T") == 0 || strcmp(reg, "E") == 0;
            if (!ok) {
                snprintf(p->err, sizeof(p->err), "line %d: 未知寄存器 %s", t->line, t->text);
                return NULL;
            }
            Expr *e = expr_new(EXPR_REGDIR);
            e->name = xstrdup(reg);
            e->type_size = 4;
            e->is_unsigned = 1;
            e->line = t->line;
            return e;
        }
        Expr *e = expr_new(EXPR_VAR);
        e->name = xstrdup(t->text);
        e->line = t->line;
        return e;
    }
    if (t->kind == T_OP && strcmp(t->text, "(") == 0) {
        Expr *e = parse_expr(p);
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
            call->l = e;
            call->name = e->name ? xstrdup(e->name) : NULL;
            call->line = e->line;
            if (call->name && strcmp(call->name, "__builtin_va_arg") == 0) {
                int acap = 0;
                Expr *a = parse_assign(p);
                if (!a) { expr_free(e); expr_free(call); return NULL; }
                push_expr(&call->args, &call->nargs, &acap, a);
                if (accept_op(p, ",")) {
                    TypeInfo ty = parse_type_spec(p);
                    while (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "*") == 0) {
                        next(p);
                        ty.ptr_depth++;
                    }
                    int sz = ty.ptr_depth > 0 ? 4 : (ty.size > 0 ? ty.size : 4);
                    Expr *szn = expr_new(EXPR_NUM);
                    szn->ival = sz;
                    szn->type_size = 4;
                    push_expr(&call->args, &call->nargs, &acap, szn);
                    free(ty.dims); free(ty.struct_name); free(ty.enum_name); free(ty.func_ret_struct_name);
                }
                if (!expect_op(p, ")")) { expr_free(e); expr_free(call); return NULL; }
                e = call;
                continue;
            }
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
            e = call;
        } else if (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "[") == 0) {
            next(p);
            Expr *idx = parse_assign(p);
            if (!idx) { expr_free(e); return NULL; }
            if (!expect_op(p, "]")) { expr_free(e); expr_free(idx); return NULL; }
            Expr *n = expr_new(EXPR_INDEX);
            n->l = e;
            n->r = idx;
            e = n;
        } else if ((peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, ".") == 0) ||
                   (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "->") == 0)) {
            int arrow = strcmp(peek(p,0)->text, "->") == 0;
            next(p);
            Token *mn = expect_kind(p, T_ID);
            if (!mn) { expr_free(e); return NULL; }
            Expr *n = expr_new(EXPR_MEMBER);
            n->l = e;
            n->member = xstrdup(mn->text);
            n->arrow = arrow;
            e = n;
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
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text, "sizeof") == 0) {
        next(p);
        if (accept_op(p, "(")) {
            int saved = p->i;
            if (is_type(p)) {
                TypeInfo ty = parse_type_spec(p);
                while (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "*") == 0) {
                    next(p);
                    ty.ptr_depth++;
                }
                if (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, ")") == 0) {
                    next(p);
                    int sz = ty.ptr_depth > 0 ? 4 : (ty.size > 0 ? ty.size : 4);
                    Expr *e = expr_new(EXPR_NUM);
                    e->ival = sz;
                    e->type_size = 4;
                    e->line = peek(p,0)->line;
                    free(ty.dims); free(ty.struct_name); free(ty.enum_name); free(ty.func_ret_struct_name);
                    return e;
                }
                free(ty.dims); free(ty.struct_name); free(ty.enum_name); free(ty.func_ret_struct_name);
                p->i = saved;
            }
            Expr *sub = parse_assign(p);
            if (!sub) return NULL;
            if (!expect_op(p, ")")) { expr_free(sub); return NULL; }
            Expr *e = expr_new(EXPR_SIZEOF);
            e->r = sub;
            e->type_size = 4;
            e->line = sub->line;
            return e;
        } else {
            Expr *sub = parse_unary(p);
            if (!sub) return NULL;
            Expr *e = expr_new(EXPR_SIZEOF);
            e->r = sub;
            e->type_size = 4;
            e->line = sub->line;
            return e;
        }
    }
    /* 强制类型转换 (int)/(float)/(double) expr */
    if (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "(") == 0) {
        Token *nt = peek(p,1);
        int is_cast_type = nt->kind == T_KW &&
            (strcmp(nt->text,"int")==0 || strcmp(nt->text,"char")==0 ||
             strcmp(nt->text,"short")==0 || strcmp(nt->text,"long")==0 ||
             strcmp(nt->text,"unsigned")==0 || strcmp(nt->text,"signed")==0 ||
             strcmp(nt->text,"const")==0 || strcmp(nt->text,"float")==0 ||
             strcmp(nt->text,"double")==0 || strcmp(nt->text,"void")==0);
        if (is_cast_type) {
            next(p); /* ( */
            TypeInfo ty = parse_type_spec(p);
            while (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "*") == 0) {
                next(p);
                ty.ptr_depth++;
            }
            if (!expect_op(p, ")")) { free(ty.dims); free(ty.struct_name); free(ty.enum_name); free(ty.func_ret_struct_name); return NULL; }
            Expr *e = expr_new(EXPR_CAST);
            e->type_size = ty.ptr_depth > 0 ? 4 : (ty.size > 0 ? ty.size : 4);
            e->is_float = ty.is_float;
            e->is_double = ty.is_double;
            e->is_unsigned = ty.ptr_depth > 0 ? 1 : ty.is_unsigned;
            e->r = parse_unary(p);
            free(ty.dims);
            free(ty.struct_name);
            free(ty.enum_name);
            free(ty.func_ret_struct_name);
            if (!e->r) { expr_free(e); return NULL; }
            return e;
        }
    }
    if (peek(p,0)->kind == T_OP && (strcmp(peek(p,0)->text,"-")==0 ||
        strcmp(peek(p,0)->text,"!")==0 || strcmp(peek(p,0)->text,"~")==0 ||
        strcmp(peek(p,0)->text,"&")==0 || strcmp(peek(p,0)->text,"*")==0)) {
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
    Expr *e = parse_binary(p, 1);
    if (!e) return NULL;
    if (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "?") == 0) {
        next(p);
        Expr *a = parse_assign(p);
        if (!a) { expr_free(e); return NULL; }
        if (!expect_op(p, ":")) { expr_free(e); expr_free(a); return NULL; }
        Expr *b = parse_cond(p);
        if (!b) { expr_free(e); expr_free(a); return NULL; }
        Expr *n = expr_new(EXPR_COND);
        n->l = e;
        n->r = a;
        n->c = b;
        return n;
    }
    return e;
}

static Expr *parse_expr(Parser *p) {
    Expr *e = parse_assign(p);
    if (!e) return NULL;
    while (accept_op(p, ",")) {
        Expr *r = parse_assign(p);
        if (!r) { expr_free(e); return NULL; }
        Expr *n = expr_new(EXPR_COMMA);
        n->l = e;
        n->r = r;
        e = n;
    }
    return e;
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

static int parse_init_list_cap(Parser *p, Expr ***out, int *n, int *cap);

static int parse_init_list(Parser *p, Expr ***out, int *n) {
    int cap = 0;
    return parse_init_list_cap(p, out, n, &cap);
}

static int parse_init_list_cap(Parser *p, Expr ***out, int *n, int *cap) {
    if (!expect_op(p, "{")) return 0;
    while (peek(p,0)->kind != T_EOF && strcmp(peek(p,0)->text, "}") != 0) {
        if (accept_op(p, ",")) continue;
        if (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "{") == 0) {
            if (!parse_init_list_cap(p, out, n, cap)) return 0;
        } else {
            Expr *e = parse_assign(p);
            if (!e) return 0;
            push_expr(out, n, cap, e);
        }
        if (!accept_op(p, ",")) break;
    }
    if (!expect_op(p, "}")) return 0;
    return 1;
}

static int is_case_or_default(Parser *p) {
    return peek(p,0)->kind == T_KW &&
        (strcmp(peek(p,0)->text, "case") == 0 || strcmp(peek(p,0)->text, "default") == 0);
}

static Stmt *parse_switch_stmt(Parser *p) {
    next(p); /* switch */
    Stmt *sw = stmt_new(STMT_SWITCH);
    if (!expect_op(p, "(")) { stmt_free(sw); return NULL; }
    sw->cond = parse_assign(p);
    if (!sw->cond || !expect_op(p, ")")) { stmt_free(sw); return NULL; }
    if (!expect_op(p, "{")) { stmt_free(sw); return NULL; }
    Stmt **items = NULL;
    int n = 0, cap = 0;
    while (peek(p,0)->kind != T_EOF && strcmp(peek(p,0)->text, "}") != 0) {
        if (is_case_or_default(p)) {
            Stmt *c = stmt_new(STMT_CASE);
            if (strcmp(peek(p,0)->text, "case") == 0) {
                next(p);
                c->expr = parse_assign(p);
                if (!c->expr || !expect_op(p, ":")) { stmt_free(c); stmt_free(sw); return NULL; }
            } else {
                next(p);
                if (!expect_op(p, ":")) { stmt_free(c); stmt_free(sw); return NULL; }
            }
            Stmt **body_items = NULL;
            int bn = 0, bcap = 0;
            while (peek(p,0)->kind != T_EOF && !is_case_or_default(p) &&
                   strcmp(peek(p,0)->text, "}") != 0) {
                Stmt *sub = parse_stmt(p);
                if (!sub) { stmt_free(c); stmt_free(sw); return NULL; }
                push_stmt(&body_items, &bn, &bcap, sub);
            }
            c->body = stmt_new(STMT_BLOCK);
            c->body->items = body_items;
            c->body->nitems = bn;
            push_stmt(&items, &n, &cap, c);
        } else {
            Stmt *sub = parse_stmt(p);
            if (!sub) { stmt_free(sw); return NULL; }
            push_stmt(&items, &n, &cap, sub);
        }
    }
    if (!expect_op(p, "}")) { stmt_free(sw); return NULL; }
    sw->body = stmt_new(STMT_BLOCK);
    sw->body->items = items;
    sw->body->nitems = n;
    return sw;
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
    if (peek(p,0)->kind == T_ID &&
        (strcmp(peek(p,0)->text, "__asm__") == 0 || strcmp(peek(p,0)->text, "asm") == 0)) {
        next(p);
        if (!expect_op(p, "(")) return NULL;
        Stmt *s = stmt_new(STMT_ASM);
        if (peek(p,0)->kind != T_STR) {
            snprintf(p->err, sizeof(p->err), "line %d: __asm__ 期望字符串字面量", peek(p,0)->line);
            stmt_free(s);
            return NULL;
        }
        s->asm_text = xstrdup("");
        while (peek(p,0)->kind == T_STR) {
            Token *st = next(p);
            size_t old = strlen(s->asm_text);
            size_t add = strlen(st->text);
            s->asm_text = (char *)realloc(s->asm_text, old + add + 1);
            memcpy(s->asm_text + old, st->text, add + 1);
        }
        if (!expect_op(p, ")")) { stmt_free(s); return NULL; }
        if (!expect_op(p, ";")) { stmt_free(s); return NULL; }
        return s;
    }
    int local_static = 0;
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text, "static") == 0) {
        next(p);
        local_static = 1;
    }
    if (is_type(p)) {
        TypeInfo ty = parse_type_spec(p);
        char *name = NULL;
        if (!parse_declarator(p, &ty, &name)) { free(ty.dims); free(name); return NULL; }
        Stmt *s = stmt_new(STMT_DECL);
        s->name = name;
        s->decl_is_static = local_static;
        int storage_size, elem_size, base_size;
        compute_type_sizes(&ty, &storage_size, &elem_size, &base_size);
        s->decl_size = storage_size;
        s->decl_unsigned = ty.is_unsigned;
        s->decl_float = ty.is_float;
        s->decl_double = ty.is_double;
        s->decl_const = ty.is_const;
        s->decl_bool = ty.is_bool;
        s->decl_ptr_depth = ty.ptr_depth;
        s->decl_is_array = ty.is_array;
        s->decl_ndims = ty.ndims;
        s->decl_dims = ty.dims;
        s->decl_array_len = ty.array_len;
        s->decl_elem_size = elem_size;
        s->decl_base_size = base_size;
        s->decl_is_struct = ty.is_struct || ty.is_union;
        if (ty.struct_name) {
            s->decl_struct_name = xstrdup(ty.struct_name);
            free(ty.struct_name);
            ty.struct_name = NULL;
        }
        s->decl_is_func_ptr = ty.is_func_ptr;
        s->decl_func_ret_size = ty.func_ret_size;
        s->decl_func_ret_float = ty.func_ret_float;
        s->decl_func_ret_double = ty.func_ret_double;
        s->decl_func_ret_void = ty.func_ret_void;
        s->decl_func_ret_is_struct = ty.func_ret_is_struct;
        if (ty.func_ret_struct_name) {
            s->decl_func_ret_struct_name = xstrdup(ty.func_ret_struct_name);
            free(ty.func_ret_struct_name);
            ty.func_ret_struct_name = NULL;
        }
        if (accept_op(p, "=")) {
            if (peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "{") == 0) {
                if (!parse_init_list(p, &s->init_list, &s->n_init_list)) { stmt_free(s); return NULL; }
                if (s->decl_array_len == 0) {
                    int len = s->n_init_list > 0 ? s->n_init_list : 1;
                    s->decl_array_len = len;
                    s->decl_ndims = 1;
                    s->decl_dims = (int *)realloc(s->decl_dims, sizeof(int));
                    s->decl_dims[0] = len;
                    s->decl_size = len * (s->decl_base_size > 0 ? s->decl_base_size : 1);
                }
            } else if (peek(p,0)->kind == T_STR && s->decl_is_array && s->decl_base_size == 1) {
                Token *st = next(p);
                s->str_init = xstrdup(st->text);
                while (peek(p,0)->kind == T_STR) {
                    Token *n = next(p);
                    size_t old = strlen(s->str_init);
                    size_t add = strlen(n->text);
                    s->str_init = (char *)realloc(s->str_init, old + add + 1);
                    memcpy(s->str_init + old, n->text, add + 1);
                }
                s->has_str_init = 1;
                if (s->decl_array_len == 0) {
                    int len = (int)strlen(s->str_init) + 1;
                    s->decl_array_len = len;
                    s->decl_ndims = 1;
                    s->decl_dims = (int *)realloc(s->decl_dims, sizeof(int));
                    s->decl_dims[0] = len;
                    s->decl_size = len * (s->decl_base_size > 0 ? s->decl_base_size : 1);
                }
            } else {
                s->expr = parse_assign(p);
            }
        }
        if (!expect_op(p, ";")) { stmt_free(s); return NULL; }
        return s;
    }
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text,"return")==0) {
        next(p);
        Stmt *s = stmt_new(STMT_RETURN);
        if (!accept_op(p, ";")) {
            s->expr = parse_expr(p);
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
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text,"switch")==0) {
        return parse_switch_stmt(p);
    }
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text,"do")==0) {
        next(p);
        Stmt *s = stmt_new(STMT_DO);
        s->body = parse_stmt(p);
        if (!s->body) { stmt_free(s); return NULL; }
        if (peek(p,0)->kind != T_KW || strcmp(peek(p,0)->text,"while")!=0) {
            snprintf(p->err, sizeof(p->err), "line %d: do 后缺少 while", peek(p,0)->line);
            stmt_free(s);
            return NULL;
        }
        next(p);
        if (!expect_op(p, "(")) { stmt_free(s); return NULL; }
        s->cond = parse_assign(p);
        if (!s->cond || !expect_op(p, ")")) { stmt_free(s); return NULL; }
        if (!expect_op(p, ";")) { stmt_free(s); return NULL; }
        return s;
    }
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text,"goto")==0) {
        next(p);
        Token *lab = expect_kind(p, T_ID);
        if (!lab) return NULL;
        Stmt *s = stmt_new(STMT_GOTO);
        s->name = xstrdup(lab->text);
        if (!expect_op(p, ";")) { stmt_free(s); return NULL; }
        return s;
    }
    if (peek(p,0)->kind == T_ID && peek(p,1)->kind == T_OP && strcmp(peek(p,1)->text, ":") == 0) {
        Token *lab = next(p);
        next(p); /* : */
        Stmt *s = stmt_new(STMT_LABEL);
        s->name = xstrdup(lab->text);
        return s;
    }
    if (peek(p,0)->kind == T_KW && strcmp(peek(p,0)->text,"for")==0) {
        next(p);
        Stmt *s = stmt_new(STMT_FOR);
        if (!expect_op(p, "(")) { free(s); return NULL; }
        if (!accept_op(p, ";")) { s->init = parse_expr(p); if (!s->init || !expect_op(p, ";")) { stmt_free(s); return NULL; } }
        if (!accept_op(p, ";")) { s->cond = parse_assign(p); if (!s->cond || !expect_op(p, ";")) { stmt_free(s); return NULL; } }
        if (!accept_op(p, ")")) { s->inc = parse_expr(p); if (!s->inc || !expect_op(p, ")")) { stmt_free(s); return NULL; } }
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
    s->expr = parse_expr(p);
    if (!s->expr || !expect_op(p, ";")) { stmt_free(s); return NULL; }
    return s;
}

static int parse_enum_definition(Parser *p, TypeInfo *ty) {
    if (!expect_op(p, "{")) return 0;
    long long val = 0;
    while (peek(p,0)->kind != T_EOF && strcmp(peek(p,0)->text, "}") != 0) {
        if (accept_op(p, ",")) continue;
        Token *name = expect_kind(p, T_ID);
        if (!name) return 0;
        long long v = val;
        if (accept_op(p, "=")) {
            Token *n = next(p);
            if (n->kind != T_NUM) { snprintf(p->err, sizeof(p->err), "line %d: 枚举值必须是整数常量", n->line); return 0; }
            v = n->ival;
        }
        parser_add_enum(p, name->text, v);
        val = v + 1;
        if (!accept_op(p, ",")) break;
    }
    if (!expect_op(p, "}")) return 0;
    if (!expect_op(p, ";")) return 0;
    ty->size = 4;
    return 1;
}

static int parse_typedef(Parser *p) {
    next(p); /* typedef */
    TypeInfo ty = parse_type_spec(p);
    if ((ty.is_struct || ty.is_union) && peek(p,0)->kind == T_OP && strcmp(peek(p,0)->text, "{") == 0) {
        if (!parse_struct_body(p, &ty)) { free(ty.dims); free(ty.struct_name); free(ty.enum_name); return 0; }
    }
    char *name = NULL;
    if (!parse_declarator(p, &ty, &name)) { free(ty.dims); free(ty.struct_name); free(ty.enum_name); free(name); return 0; }
    parser_add_typedef(p, name, &ty);
    free(name);
    free(ty.dims);
    free(ty.struct_name);
    free(ty.enum_name);
    if (!expect_op(p, ";")) return 0;
    return 1;
}

Program parse_program(TokenArray *ta, char **err) {
    Program prog;
    memset(&prog, 0, sizeof(prog));
    Parser p;
    memset(&p, 0, sizeof(p));
    p.ta = ta;
    p.i = 0;

    while (peek(&p,0)->kind != T_EOF) {
        int storage_static = 0, storage_extern = 0, storage_inline = 0;
        while (peek(&p,0)->kind == T_KW &&
               (strcmp(peek(&p,0)->text, "static") == 0 ||
                strcmp(peek(&p,0)->text, "extern") == 0 ||
                strcmp(peek(&p,0)->text, "inline") == 0)) {
            if (strcmp(peek(&p,0)->text, "static") == 0) storage_static = 1;
            else if (strcmp(peek(&p,0)->text, "extern") == 0) storage_extern = 1;
            else storage_inline = 1;
            next(&p);
        }
        if (peek(&p,0)->kind == T_KW && strcmp(peek(&p,0)->text, "typedef") == 0) {
            if (!parse_typedef(&p)) break;
            continue;
        }
        if (!is_type(&p)) {
            snprintf(p.err, sizeof(p.err), "line %d: 期望类型", peek(&p,0)->line);
            break;
        }
        TypeInfo ty = parse_type_spec(&p);
        if ((ty.is_struct || ty.is_union) && peek(&p,0)->kind == T_OP && strcmp(peek(&p,0)->text, "{") == 0) {
            if (!parse_struct_definition(&p, &ty)) { free(ty.dims); free(ty.struct_name); free(ty.enum_name); break; }
            free(ty.dims);
            free(ty.struct_name);
            free(ty.enum_name);
            continue;
        }
        if (ty.is_enum && peek(&p,0)->kind == T_OP && strcmp(peek(&p,0)->text, "{") == 0) {
            if (!parse_enum_definition(&p, &ty)) { free(ty.dims); free(ty.struct_name); free(ty.enum_name); break; }
            free(ty.dims);
            free(ty.struct_name);
            free(ty.enum_name);
            continue;
        }
        char *name = NULL;
        if (!parse_declarator(&p, &ty, &name)) { free(ty.dims); free(ty.struct_name); free(name); break; }

        if (peek(&p,0)->kind == T_OP && strcmp(peek(&p,0)->text, "(") == 0) {
            next(&p);
            Function f;
            memset(&f, 0, sizeof(f));
            f.name = name;
            name = NULL;
            f.is_static = storage_static;
            f.is_inline = storage_inline;
            if (storage_extern) f.is_decl = 1;
            int ret_storage, ret_elem, ret_base;
            compute_type_sizes(&ty, &ret_storage, &ret_elem, &ret_base);
            f.ret_void = ty.is_void;
            f.ret_size = ret_storage;
            f.ret_elem_size = ret_elem;
            f.ret_base_size = ret_base;
            f.ret_ptr_depth = ty.ptr_depth;
            f.ret_unsigned = ty.ptr_depth ? 1 : ty.is_unsigned;
            f.ret_float = ty.ptr_depth ? 0 : ty.is_float;
            f.ret_double = ty.ptr_depth ? 0 : ty.is_double;
            f.ret_bool = ty.ptr_depth ? 0 : ty.is_bool;
            f.ret_is_struct = ty.is_struct || ty.is_union;
            if (ty.struct_name) {
                f.ret_struct_name = xstrdup(ty.struct_name);
                free(ty.struct_name);
                ty.struct_name = NULL;
            }
            if (!accept_op(&p, ")")) {
                if (peek(&p,0)->kind == T_KW && strcmp(peek(&p,0)->text,"void")==0 &&
                    peek(&p,1)->kind == T_OP && strcmp(peek(&p,1)->text,")")==0) {
                    next(&p);
                    expect_op(&p, ")");
                } else {
                    while (1) {
                        if (peek(&p,0)->kind == T_OP && strcmp(peek(&p,0)->text, "...") == 0) {
                            next(&p);
                            f.is_vararg = 1;
                            break;
                        }
                        if (!is_type(&p) || strcmp(peek(&p,0)->text,"void")==0) {
                            snprintf(p.err, sizeof(p.err), "line %d: 期望参数类型", peek(&p,0)->line);
                            break;
                        }
                        TypeInfo pt = parse_type_spec(&p);
                        char *pn = NULL;
                        if (!parse_declarator(&p, &pt, &pn)) { free(pt.dims); free(pn); break; }
                        /* 数组参数退化为指针（多维数组参数暂按一维退化处理） */
                        if (pt.is_array) {
                            pt.ptr_depth++;
                            pt.is_array = 0;
                            pt.ndims = 0;
                            free(pt.dims);
                            pt.dims = NULL;
                            pt.array_len = 0;
                        }
                        int pst, pe, pb;
                        compute_type_sizes(&pt, &pst, &pe, &pb);
                        f.params = (char **)realloc(f.params, (size_t)(f.nparams+1)*sizeof(char *));
                        f.param_sizes = (int *)realloc(f.param_sizes, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_unsigned = (int *)realloc(f.param_unsigned, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_float = (int *)realloc(f.param_float, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_double = (int *)realloc(f.param_double, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_const = (int *)realloc(f.param_const, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_bool = (int *)realloc(f.param_bool, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_ptr_depth = (int *)realloc(f.param_ptr_depth, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_elem_size = (int *)realloc(f.param_elem_size, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_base_size = (int *)realloc(f.param_base_size, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_is_struct = (int *)realloc(f.param_is_struct, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_struct_name = (char **)realloc(f.param_struct_name, (size_t)(f.nparams+1)*sizeof(char *));
                        f.param_is_func_ptr = (int *)realloc(f.param_is_func_ptr, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_func_ret_size = (int *)realloc(f.param_func_ret_size, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_func_ret_float = (int *)realloc(f.param_func_ret_float, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_func_ret_double = (int *)realloc(f.param_func_ret_double, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_func_ret_void = (int *)realloc(f.param_func_ret_void, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_func_ret_is_struct = (int *)realloc(f.param_func_ret_is_struct, (size_t)(f.nparams+1)*sizeof(int));
                        f.param_func_ret_struct_name = (char **)realloc(f.param_func_ret_struct_name, (size_t)(f.nparams+1)*sizeof(char *));
                        f.param_sizes[f.nparams] = pst;
                        f.param_unsigned[f.nparams] = pt.is_unsigned;
                        f.param_float[f.nparams] = pt.is_float;
                        f.param_double[f.nparams] = pt.is_double;
                        f.param_const[f.nparams] = pt.is_const;
                        f.param_bool[f.nparams] = pt.is_bool;
                        f.param_ptr_depth[f.nparams] = pt.ptr_depth;
                        f.param_elem_size[f.nparams] = pe;
                        f.param_base_size[f.nparams] = pb;
                        f.param_is_struct[f.nparams] = pt.is_struct || pt.is_union;
                        f.param_struct_name[f.nparams] = pt.struct_name ? xstrdup(pt.struct_name) : NULL;
                        if (pt.struct_name) { free(pt.struct_name); pt.struct_name = NULL; }
                        f.param_is_func_ptr[f.nparams] = pt.is_func_ptr;
                        f.param_func_ret_size[f.nparams] = pt.func_ret_size;
                        f.param_func_ret_float[f.nparams] = pt.func_ret_float;
                        f.param_func_ret_double[f.nparams] = pt.func_ret_double;
                        f.param_func_ret_void[f.nparams] = pt.func_ret_void;
                        f.param_func_ret_is_struct[f.nparams] = pt.func_ret_is_struct;
                        f.param_func_ret_struct_name[f.nparams] = pt.func_ret_struct_name ? xstrdup(pt.func_ret_struct_name) : NULL;
                        if (pt.func_ret_struct_name) { free(pt.func_ret_struct_name); pt.func_ret_struct_name = NULL; }
                        f.params[f.nparams++] = pn;
                        if (peek(&p,0)->kind == T_OP && strcmp(peek(&p,0)->text, "...") == 0) {
                            next(&p);
                            f.is_vararg = 1;
                            break;
                        }
                        if (!accept_op(&p, ",")) break;
                    }
                    if (p.err[0] || !expect_op(&p, ")")) break;
                }
            }
            if (peek(&p,0)->kind == T_KW && strcmp(peek(&p,0)->text, "__interrupt__") == 0) {
                next(&p);
                f.is_isr = 1;
                if (f.nparams != 0 || !f.ret_void) {
                    snprintf(p.err, sizeof(p.err), "line %d: __interrupt__ 函数必须为 void 且无参数", peek(&p,0)->line);
                    break;
                }
            }
            if (accept_op(&p, ";")) {
                f.is_decl = 1;
                prog.funcs = (Function *)realloc(prog.funcs, (size_t)(prog.nfuncs+1)*sizeof(Function));
                prog.funcs[prog.nfuncs++] = f;
                continue;
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
            g.name = name;
            name = NULL;
            g.has_init = 0;
            g.init = 0;
            g.is_extern = storage_extern;
            g.is_static = storage_static;
            int gst, ge, gb;
            compute_type_sizes(&ty, &gst, &ge, &gb);
            g.type_size = gst;
            g.is_unsigned = ty.is_unsigned;
            g.is_float = ty.is_float;
            g.is_double = ty.is_double;
            g.is_const = ty.is_const;
            g.is_bool = ty.is_bool;
            g.ptr_depth = ty.ptr_depth;
            g.is_array = ty.is_array;
            g.ndims = ty.ndims;
            g.dims = ty.dims;
            ty.dims = NULL;
            g.array_len = ty.array_len;
            g.elem_size = ge;
            g.base_size = gb;
            g.is_struct = ty.is_struct || ty.is_union;
            if (ty.struct_name) {
                g.struct_name = xstrdup(ty.struct_name);
                free(ty.struct_name);
                ty.struct_name = NULL;
            }
            g.is_func_ptr = ty.is_func_ptr;
            g.func_ret_size = ty.func_ret_size;
            g.func_ret_float = ty.func_ret_float;
            g.func_ret_double = ty.func_ret_double;
            g.func_ret_void = ty.func_ret_void;
            g.func_ret_is_struct = ty.func_ret_is_struct;
            if (ty.func_ret_struct_name) {
                g.func_ret_struct_name = xstrdup(ty.func_ret_struct_name);
                free(ty.func_ret_struct_name);
                ty.func_ret_struct_name = NULL;
            }
            if (accept_op(&p, "=")) {
                Token *v = peek(&p, 0);
                if (v->kind == T_OP && strcmp(v->text, "{") == 0) {
                    Expr **items = NULL;
                    int nitems = 0;
                    if (!parse_init_list(&p, &items, &nitems)) { for (int z=0; z<nitems; z++) expr_free(items[z]); free(items); break; }
                    g.init_list = (long long *)malloc((size_t)(nitems > 0 ? nitems : 1) * sizeof(long long));
                    g.n_init_list = 0;
                    for (int z = 0; z < nitems; z++) {
                        if (items[z]->kind != EXPR_NUM) {
                            snprintf(p.err, sizeof(p.err), "line %d: 全局数组初始化器必须是常量", items[z]->line);
                            for (int k=0; k<nitems; k++) expr_free(items[k]);
                            free(items);
                            free(g.init_list);
                            g.init_list = NULL;
                            break;
                        }
                        g.init_list[g.n_init_list++] = items[z]->ival;
                        expr_free(items[z]);
                    }
                    free(items);
                    if (p.err[0]) break;
                    if (g.is_array && g.array_len == 0) {
                        int len = g.n_init_list > 0 ? g.n_init_list : 1;
                        g.array_len = len;
                        g.ndims = 1;
                        g.dims = (int *)realloc(g.dims, sizeof(int));
                        g.dims[0] = len;
                        g.type_size = len * (g.base_size > 0 ? g.base_size : 1);
                    }
                } else if (v->kind == T_STR) {
                    next(&p);
                    g.has_str_init = 1;
                    g.str_init = xstrdup(v->text);
                    while (peek(&p,0)->kind == T_STR) {
                        Token *n = next(&p);
                        size_t old = strlen(g.str_init);
                        size_t add = strlen(n->text);
                        g.str_init = (char *)realloc(g.str_init, old + add + 1);
                        memcpy(g.str_init + old, n->text, add + 1);
                    }
                    if (g.is_array && g.array_len == 0) {
                        int len = (int)strlen(g.str_init) + 1;
                        g.array_len = len;
                        g.ndims = 1;
                        g.dims = (int *)realloc(g.dims, sizeof(int));
                        g.dims[0] = len;
                        g.type_size = len * (g.base_size > 0 ? g.base_size : 1);
                    }
                } else if (v->kind != T_NUM) {
                    snprintf(p.err, sizeof(p.err), "line %d: 全局初始化器必须是常量", v->line);
                    break;
                } else {
                    next(&p);
                    g.has_init = 1;
                    g.init = v->ival;
                }
            }
            if (!expect_op(&p, ";")) break;
            prog.globals = (Global *)realloc(prog.globals, (size_t)(prog.nglobals+1)*sizeof(Global));
            prog.globals[prog.nglobals++] = g;
        }
    }

    prog.structs = p.structs;
    prog.nstructs = p.nstructs;
    p.structs = NULL;
    p.nstructs = 0;

    for (int i = 0; i < p.ntypedefs; i++) {
        free(p.typedefs[i].name);
        free(p.typedefs[i].dims);
        free(p.typedefs[i].struct_name);
        free(p.typedefs[i].enum_name);
        free(p.typedefs[i].func_ret_struct_name);
    }
    free(p.typedefs);
    for (int i = 0; i < p.nenums; i++) free(p.enum_names[i]);
    free(p.enum_names);
    free(p.enum_values);

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
