#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>

typedef struct {
    char *name;
    int offset;
    int size;
    int is_unsigned;
    int is_float;
    int is_double;
    int is_const;
    int is_bool;
    int ptr_depth;
    int is_array;
    int ndims;
    int *dims;
    int array_len;
    int elem_size;
    int base_size;
    int is_struct;
    char *struct_name;
    int is_func_ptr;
    int func_ret_size;
    int func_ret_float;
    int func_ret_double;
    int func_ret_void;
    int func_ret_is_struct;
    char *func_ret_struct_name;
    char *static_label;
} VarInfo;

typedef struct {
    char *name;
    int offset;
    int size;
    int is_unsigned;
    int is_float;
    int is_double;
    int is_const;
    int is_bool;
    int ptr_depth;
    int is_array;
    int ndims;
    int *dims;
    int array_len;
    int elem_size;
    int base_size;
    int is_struct;
    char *struct_name;
    int is_func_ptr;
    int func_ret_size;
    int func_ret_float;
    int func_ret_double;
    int func_ret_void;
    int func_ret_is_struct;
    char *func_ret_struct_name;
} ParamInfo;

typedef struct {
    char *brk;
    char *cont;
} LoopInfo;

typedef struct {
    char *str;
    char *label;
} StrEntry;

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
    Function *cur_func;
    int cur_func_named_bytes;
    int cur_func_is_vararg;
    StrEntry *strings;
    int nstrings, capstrings;
    int str_cnt;
    int data_off;
} CodeGen;

static VarInfo *local_info(CodeGen *cg, const char *name);
static ParamInfo *param_info(CodeGen *cg, const char *name);
static void emit_store_to_b(CodeGen *cg, int size);
static void emit_load_from_b(CodeGen *cg, int size, int is_unsigned);
static int expr_is_float(CodeGen *cg, Expr *e);
static int expr_is_double(CodeGen *cg, Expr *e);

/* 把 double 值编码为 80 位扩展精度（IEEE 754 x87）立即数 */
static void double_to_ext80(double d, unsigned long long *lo, unsigned int *hi) {
    long double x = (long double)d;
    int sign = signbit(x) ? 1 : 0;
    uint16_t exp = 0;
    uint64_t mant = 0;
    if (isnan(x)) {
        exp = 0x7fff;
        mant = 0xc000000000000000ULL;
    } else if (isinf(x)) {
        exp = 0x7fff;
        mant = 0x8000000000000000ULL;
    } else if (x == 0.0L) {
        exp = 0;
        mant = 0;
    } else {
        int e = 0;
        long double m = frexpl(x, &e);
        m = fabsl(m);
        long double scaled = ldexpl(m, 64);
        if (scaled >= 18446744073709551616.0L) mant = 0xFFFFFFFFFFFFFFFFULL;
        else mant = (uint64_t)scaled;
        if (mant == 0) mant = 1;
        exp = (uint16_t)(e + 16382);
    }
    uint8_t b[10];
    memset(b, 0, sizeof(b));
    b[8] = (uint8_t)(exp & 0xff);
    b[9] = (uint8_t)(((exp >> 8) & 0x7f) | (sign ? 0x80 : 0));
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)((mant >> (8 * i)) & 0xff);
    *lo = 0;
    for (int i = 0; i < 8; i++) *lo |= ((unsigned long long)b[i]) << (8 * i);
    *hi = (unsigned int)b[8] | ((unsigned int)b[9] << 8);
}

static void emit_conv_to_int(CodeGen *cg, Expr *e);

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

static const char *cg_str_label(CodeGen *cg, const char *str) {
    for (int i = 0; i < cg->nstrings; i++) {
        if (strcmp(cg->strings[i].str, str) == 0) return cg->strings[i].label;
    }
    if (cg->nstrings >= cg->capstrings) {
        cg->capstrings = cg->capstrings ? cg->capstrings * 2 : 16;
        cg->strings = (StrEntry *)realloc(cg->strings, (size_t)cg->capstrings * sizeof(StrEntry));
    }
    char label[32];
    snprintf(label, sizeof(label), "str%d", cg->str_cnt++);
    cg->strings[cg->nstrings].str = xstrdup(str);
    cg->strings[cg->nstrings].label = xstrdup(label);
    return cg->strings[cg->nstrings++].label;
}

static void collect_expr_strings(CodeGen *cg, Expr *e) {
    if (!e) return;
    if (e->kind == EXPR_STR) cg_str_label(cg, e->name);
    collect_expr_strings(cg, e->l);
    collect_expr_strings(cg, e->r);
    for (int i = 0; i < e->nargs; i++) collect_expr_strings(cg, e->args[i]);
}

static void collect_stmt_strings(CodeGen *cg, Stmt *s) {
    if (!s) return;
    if (s->kind == STMT_DECL && s->has_str_init) cg_str_label(cg, s->str_init);
    collect_expr_strings(cg, s->expr);
    collect_expr_strings(cg, s->cond);
    collect_expr_strings(cg, s->init);
    collect_expr_strings(cg, s->inc);
    collect_stmt_strings(cg, s->then);
    collect_stmt_strings(cg, s->els);
    collect_stmt_strings(cg, s->body);
    for (int i = 0; i < s->nitems; i++) collect_stmt_strings(cg, s->items[i]);
}

static void collect_program_strings(CodeGen *cg, Program *p) {
    for (int i = 0; i < p->nglobals; i++) {
        Global *g = &p->globals[i];
        if (g->has_str_init && !(g->is_array && g->base_size == 1)) cg_str_label(cg, g->str_init);
    }
    for (int i = 0; i < p->nfuncs; i++) {
        Function *f = &p->funcs[i];
        for (int j = 0; j < f->nbody; j++) collect_stmt_strings(cg, f->body[j]);
    }
}

static void emit_str_data(CodeGen *cg, const char *label, const char *s) {
    fprintf(cg->out, "%s:\n", label);
    size_t n = strlen(s) + 1;
    for (size_t i = 0; i < n; i++) {
        unsigned char v = (i < strlen(s)) ? (unsigned char)s[i] : 0;
        fprintf(cg->out, "\tDB %d, 0x%02X\n", cg->data_off++, v);
    }
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

static StructDef *struct_info(CodeGen *cg, const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < cg->prog->nstructs; i++)
        if (strcmp(cg->prog->structs[i].name, name) == 0) return &cg->prog->structs[i];
    return NULL;
}

static MemberDef *member_def(CodeGen *cg, const char *sname, const char *mname) {
    StructDef *d = struct_info(cg, sname);
    if (!d) return NULL;
    for (int i = 0; i < d->nmembers; i++)
        if (strcmp(d->members[i].name, mname) == 0) return &d->members[i];
    return NULL;
}

static int member_offset(CodeGen *cg, const char *sname, const char *mname) {
    MemberDef *m = member_def(cg, sname, mname);
    return m ? m->offset : 0;
}

static void emit_load_bitfield(CodeGen *cg, MemberDef *m) {
    /* A 已指向位域所在存储单元 */
    cg_emit(cg, "LR DWORD A, *A");
    if (m->bit_offset) cg_emit(cg, "SHR DWORD A, %d", m->bit_offset);
    if (m->bit_width < 32) {
        uint32_t mask = (1u << m->bit_width) - 1u;
        cg_emit(cg, "LET B, DWORD 0x%X", mask);
        cg_emit(cg, "AND DWORD A, B");
        if (!m->is_unsigned) {
            int shift = 32 - m->bit_width;
            cg_emit(cg, "SHL DWORD A, %d", shift);
            cg_emit(cg, "MSR DWORD A, %d", shift);
        }
    }
}

static void emit_store_bitfield(CodeGen *cg, MemberDef *m) {
    /* 调用前已把要写入的值放在 A；这里完成读-改-写 */
    cg_emit(cg, "PUSH DWORD A");
    /* 左值地址在 gen_assign 中已通过 gen_lvalue_addr 得到；此函数期望 A=地址 */
    cg_emit(cg, "MOV B, A");
    cg_emit(cg, "LR DWORD A, *B");
    uint32_t width_mask = (m->bit_width == 32) ? 0xFFFFFFFFu : ((1u << m->bit_width) - 1u);
    uint32_t field_mask = width_mask << m->bit_offset;
    cg_emit(cg, "LET R, DWORD 0x%X", ~field_mask);
    cg_emit(cg, "AND DWORD A, R");
    cg_emit(cg, "POP DWORD C");
    if (m->bit_offset) cg_emit(cg, "SHL DWORD C, %d", m->bit_offset);
    cg_emit(cg, "LET R, DWORD 0x%X", field_mask);
    cg_emit(cg, "AND DWORD C, R");
    cg_emit(cg, "OR DWORD A, C");
    cg_emit(cg, "ST DWORD *B, A");
}


static void var_func_ret_info(CodeGen *cg, Expr *e, int *rsz, int *rf, int *rd, int *rvoid, int *ris, const char **rsname) {
    *rsz = 4; *rf = 0; *rd = 0; *rvoid = 0; *ris = 0; *rsname = NULL;
    if (e->kind != EXPR_VAR) return;
    VarInfo *li = local_info(cg, e->name);
    if (li) {
        *rsz = li->func_ret_size ? li->func_ret_size : 4;
        *rf = li->func_ret_float; *rd = li->func_ret_double; *rvoid = li->func_ret_void;
        *ris = li->func_ret_is_struct; *rsname = li->func_ret_struct_name;
        return;
    }
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) {
        *rsz = pi->func_ret_size ? pi->func_ret_size : 4;
        *rf = pi->func_ret_float; *rd = pi->func_ret_double; *rvoid = pi->func_ret_void;
        *ris = pi->func_ret_is_struct; *rsname = pi->func_ret_struct_name;
        return;
    }
    Global *g = global_info(cg->prog, e->name);
    if (g) {
        *rsz = g->func_ret_size ? g->func_ret_size : 4;
        *rf = g->func_ret_float; *rd = g->func_ret_double; *rvoid = g->func_ret_void;
        *ris = g->func_ret_is_struct; *rsname = g->func_ret_struct_name;
    }
}

static int var_is_struct(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 0;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->is_struct && li->ptr_depth == 0 && !li->is_array;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->is_struct && pi->ptr_depth == 0 && !pi->is_array;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->is_struct && g->ptr_depth == 0 && !g->is_array;
    return 0;
}

static const char *var_struct_name(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return NULL;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->struct_name;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->struct_name;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->struct_name;
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

static int var_is_float(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 0;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->ptr_depth == 0 && !li->is_array && li->is_float;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->ptr_depth == 0 && !pi->is_array && pi->is_float;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->ptr_depth == 0 && !g->is_array && g->is_float;
    return 0;
}

static int var_is_double(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 0;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->ptr_depth == 0 && !li->is_array && li->is_double;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->ptr_depth == 0 && !pi->is_array && pi->is_double;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->ptr_depth == 0 && !g->is_array && g->is_double;
    return 0;
}

static int var_is_const(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 0;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->is_const;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->is_const;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->is_const;
    return 0;
}

static int var_is_bool(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 0;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->ptr_depth == 0 && !li->is_array && li->is_bool;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->ptr_depth == 0 && !pi->is_array && pi->is_bool;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->ptr_depth == 0 && !g->is_array && g->is_bool;
    return 0;
}

static int var_ptr_depth(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 0;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->ptr_depth;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->ptr_depth;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->ptr_depth;
    return 0;
}

static int var_is_array(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 0;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->is_array;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->is_array;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->is_array;
    return 0;
}

static int var_array_ndims(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 0;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->ndims;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->ndims;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->ndims;
    return 0;
}

static int *var_dims(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return NULL;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->dims;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->dims;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->dims;
    return NULL;
}

static int var_elem_size(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 4;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->elem_size > 0 ? li->elem_size : 4;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->elem_size > 0 ? pi->elem_size : 4;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->elem_size > 0 ? g->elem_size : 4;
    return 4;
}

static int var_base_size(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 4;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->base_size > 0 ? li->base_size : li->size;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->base_size > 0 ? pi->base_size : pi->size;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->base_size > 0 ? g->base_size : g->type_size;
    return 4;
}

static int var_elem_is_float(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 0;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->is_float;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->is_float;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->is_float;
    return 0;
}

static int var_elem_is_double(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 0;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->is_double;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->is_double;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->is_double;
    return 0;
}

static int var_elem_is_bool(CodeGen *cg, Expr *e) {
    if (e->kind != EXPR_VAR) return 0;
    VarInfo *li = local_info(cg, e->name);
    if (li) return li->is_bool;
    ParamInfo *pi = param_info(cg, e->name);
    if (pi) return pi->is_bool;
    Global *g = global_info(cg->prog, e->name);
    if (g) return g->is_bool;
    return 0;
}

static void emit_bool_normalize(CodeGen *cg) {
    cg_emit(cg, "MOV C, A");
    cg_emit(cg, "ZERO T");
    cg_emit(cg, "CMP DWORD T");
    const char *l1 = cg_new_label(cg, "BL");
    const char *l2 = cg_new_label(cg, "BL");
    cg_emit(cg, "LET E, DWORD %s", l1);
    cg_emit(cg, "JZ");
    cg_emit(cg, "LET A, DWORD 1");
    cg_emit(cg, "LET E, DWORD %s", l2);
    cg_emit(cg, "JMP");
    cg_emit(cg, "%s:", l1);
    cg_emit(cg, "LET A, DWORD 0");
    cg_emit(cg, "%s:", l2);
}

static int expr_size(CodeGen *cg, Expr *e);
static int expr_unsigned(CodeGen *cg, Expr *e);
static int expr_array_ndims(CodeGen *cg, Expr *e);
static int expr_is_array(CodeGen *cg, Expr *e);
static int expr_is_pointer(CodeGen *cg, Expr *e);
static int expr_elem_size(CodeGen *cg, Expr *e);
static int expr_elem_unsigned(CodeGen *cg, Expr *e);
static int expr_elem_is_float(CodeGen *cg, Expr *e);
static int expr_elem_is_double(CodeGen *cg, Expr *e);
static int expr_elem_is_bool(CodeGen *cg, Expr *e);
static int expr_elem_is_pointer(CodeGen *cg, Expr *e);
static int expr_is_bool(CodeGen *cg, Expr *e);
static int expr_is_struct(CodeGen *cg, Expr *e);
static const char *expr_struct_name(CodeGen *cg, Expr *e);
static MemberDef *expr_member_def(CodeGen *cg, Expr *e);
static void gen_lvalue_addr(CodeGen *cg, Expr *e);

static void cg_push_local(CodeGen *cg, const char *name, int offset, int size, int is_unsigned, int is_float, int is_double, int is_const, int is_bool, int ptr_depth, int is_array, int ndims, int *dims, int array_len, int elem_size, int base_size, int is_struct, const char *struct_name, int is_func_ptr, int func_ret_size, int func_ret_float, int func_ret_double, int func_ret_void, int func_ret_is_struct, const char *func_ret_struct_name) {
    if (cg->nlocals >= cg->caplocals) {
        cg->caplocals = cg->caplocals ? cg->caplocals * 2 : 16;
        cg->locals = (VarInfo *)realloc(cg->locals, (size_t)cg->caplocals * sizeof(VarInfo));
    }
    VarInfo *v = &cg->locals[cg->nlocals];
    memset(v, 0, sizeof(*v));
    v->name = xstrdup(name);
    v->offset = offset;
    v->size = size;
    v->is_unsigned = is_unsigned;
    v->is_float = is_float;
    v->is_double = is_double;
    v->is_const = is_const;
    v->is_bool = is_bool;
    v->ptr_depth = ptr_depth;
    v->is_array = is_array;
    v->ndims = ndims;
    v->dims = dims;
    v->array_len = array_len;
    v->elem_size = elem_size;
    v->base_size = base_size;
    v->is_struct = is_struct;
    v->struct_name = struct_name ? xstrdup(struct_name) : NULL;
    v->is_func_ptr = is_func_ptr;
    v->func_ret_size = func_ret_size;
    v->func_ret_float = func_ret_float;
    v->func_ret_double = func_ret_double;
    v->func_ret_void = func_ret_void;
    v->func_ret_is_struct = func_ret_is_struct;
    v->func_ret_struct_name = func_ret_struct_name ? xstrdup(func_ret_struct_name) : NULL;
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

static void cg_push_param(CodeGen *cg, const char *name, int offset, int size, int is_unsigned, int is_float, int is_double, int is_const, int is_bool, int ptr_depth, int is_array, int ndims, int *dims, int array_len, int elem_size, int base_size, int is_struct, const char *struct_name, int is_func_ptr, int func_ret_size, int func_ret_float, int func_ret_double, int func_ret_void, int func_ret_is_struct, const char *func_ret_struct_name) {
    if (cg->nparams >= cg->capparams) {
        cg->capparams = cg->capparams ? cg->capparams * 2 : 8;
        cg->params = (ParamInfo *)realloc(cg->params, (size_t)cg->capparams * sizeof(ParamInfo));
    }
    ParamInfo *v = &cg->params[cg->nparams];
    memset(v, 0, sizeof(*v));
    v->name = xstrdup(name);
    v->offset = offset;
    v->size = size;
    v->is_unsigned = is_unsigned;
    v->is_float = is_float;
    v->is_double = is_double;
    v->is_const = is_const;
    v->is_bool = is_bool;
    v->ptr_depth = ptr_depth;
    v->is_array = is_array;
    v->ndims = ndims;
    v->dims = dims;
    v->array_len = array_len;
    v->elem_size = elem_size;
    v->base_size = base_size;
    v->is_struct = is_struct;
    v->struct_name = struct_name ? xstrdup(struct_name) : NULL;
    v->is_func_ptr = is_func_ptr;
    v->func_ret_size = func_ret_size;
    v->func_ret_float = func_ret_float;
    v->func_ret_double = func_ret_double;
    v->func_ret_void = func_ret_void;
    v->func_ret_is_struct = func_ret_is_struct;
    v->func_ret_struct_name = func_ret_struct_name ? xstrdup(func_ret_struct_name) : NULL;
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
            cg_push_local(cg, s->name, s->decl_is_static ? 0 : *frame, sz, s->decl_unsigned, s->decl_float, s->decl_double, s->decl_const, s->decl_bool,
                          s->decl_ptr_depth, s->decl_is_array, s->decl_ndims, s->decl_dims, s->decl_array_len,
                          s->decl_elem_size, s->decl_base_size, s->decl_is_struct, s->decl_struct_name,
                          s->decl_is_func_ptr, s->decl_func_ret_size, s->decl_func_ret_float,
                          s->decl_func_ret_double, s->decl_func_ret_void, s->decl_func_ret_is_struct,
                          s->decl_func_ret_struct_name);
            if (s->decl_is_static) {
                VarInfo *v = local_info(cg, s->name);
                if (v && s->static_label) v->static_label = xstrdup(s->static_label);
            } else {
                *frame += sz;
            }
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
    VarInfo *li = local_info(cg, e->name);
    if (li && li->static_label) {
        cg_emit(cg, "LET A, DWORD %s", li->static_label);
        return;
    }
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

/* 数组表达式剩余维数：只有数组变量和嵌套下标仍能保留“数组”类型 */
static int expr_array_ndims(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_VAR: return var_is_array(cg, e) ? var_array_ndims(cg, e) : 0;
        case EXPR_INDEX: {
            int n = expr_array_ndims(cg, e->l);
            return n > 1 ? n - 1 : 0;
        }
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            return m && m->is_array ? 1 : 0;
        }
        default: return 0;
    }
}

static int expr_is_array(CodeGen *cg, Expr *e) {
    return expr_array_ndims(cg, e) > 0;
}

static int expr_elem_is_pointer(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_VAR:
            if (var_is_array(cg, e)) {
                if (var_array_ndims(cg, e) > 1) return 1;
                return var_ptr_depth(cg, e) > 0;
            }
            return var_ptr_depth(cg, e) > 1;
        case EXPR_INDEX:
            if (expr_is_array(cg, e)) {
                int n = expr_array_ndims(cg, e);
                if (n > 1) return 1;
                Expr *root = e;
                while (root->kind == EXPR_INDEX) root = root->l;
                if (root->kind == EXPR_VAR && var_is_array(cg, root))
                    return var_ptr_depth(cg, root) > 0;
                return 0;
            }
            return expr_is_pointer(cg, e);
        case EXPR_UNARY:
            if (strcmp(e->op, "&") == 0) return 0;
            if (strcmp(e->op, "*") == 0) return expr_elem_is_pointer(cg, e->r);
            return 0;
        case EXPR_BIN:
            return expr_is_pointer(cg, e);
        case EXPR_ASSIGN:
            return expr_elem_is_pointer(cg, e->l);
        case EXPR_INCDEC:
            return expr_elem_is_pointer(cg, e->r ? e->r : e->l);
        default:
            return 0;
    }
}

static int expr_is_pointer(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_VAR: return var_is_array(cg, e) || var_ptr_depth(cg, e) > 0;
        case EXPR_STR: return 1;
        case EXPR_SIZEOF: return 0;
        case EXPR_COND: return expr_unsigned(cg, e->r) || expr_unsigned(cg, e->c);
        case EXPR_COMMA: return expr_unsigned(cg, e->r);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            return m && (m->ptr_depth > 0 || m->is_array);
        }
        case EXPR_INDEX: return expr_is_array(cg, e) || expr_elem_is_pointer(cg, e->l);
        case EXPR_UNARY:
            if (strcmp(e->op, "&") == 0) return 1;
            if (strcmp(e->op, "*") == 0) return expr_is_pointer(cg, e->r);
            return 0;
        case EXPR_BIN: {
            if (strcmp(e->op, "+") == 0 || strcmp(e->op, "-") == 0)
                return expr_is_pointer(cg, e->l) || expr_is_pointer(cg, e->r);
            return 0;
        }
        case EXPR_ASSIGN: return expr_is_pointer(cg, e->l);
        case EXPR_INCDEC: return expr_is_pointer(cg, e->r ? e->r : e->l);
        case EXPR_CAST: return e->type_size == 4 && !e->is_float && !e->is_double && e->r && expr_is_pointer(cg, e->r);
        default: return 0;
    }
}

static int expr_elem_size(CodeGen *cg, Expr *e) {
    if (!e) return 4;
    switch (e->kind) {
        case EXPR_VAR:
            if (var_is_array(cg, e)) return var_elem_size(cg, e);
            if (var_ptr_depth(cg, e) > 0) return var_elem_size(cg, e);
            return var_size(cg, e);
        case EXPR_STR: return 1;
        case EXPR_SIZEOF: return 0;
        case EXPR_COND: return expr_unsigned(cg, e->r) || expr_unsigned(cg, e->c);
        case EXPR_COMMA: return expr_unsigned(cg, e->r);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            if (!m) return 4;
            if (m->is_array) return m->elem_size;
            if (m->ptr_depth > 0) return m->elem_size;
            return m->size;
        }
        case EXPR_INDEX: {
            int n = expr_array_ndims(cg, e);
            if (n > 0) {
                /* 当前仍是数组：元素大小为去掉下一维后剩余数组的字节数 */
                int nd = expr_array_ndims(cg, e->l);
                if (nd <= 0) return 4;
                /* 从原数组信息推导 */
                Expr *root = e;
                while (root->kind == EXPR_INDEX) root = root->l;
                if (root->kind == EXPR_VAR && var_is_array(cg, root)) {
                    int *dims = var_dims(cg, root);
                    int total_dims = var_array_ndims(cg, root);
                    if (!dims || total_dims <= 0) return 4;
                    int bs = var_base_size(cg, root);
                    int prod = 1;
                    for (int i = total_dims - n + 1; i < total_dims; i++)
                        prod *= dims[i];
                    return bs * prod;
                }
                return 4;
            }
            return expr_elem_size(cg, e->l);
        }
        case EXPR_UNARY:
            if (strcmp(e->op, "&") == 0) return expr_size(cg, e->r);
            if (strcmp(e->op, "*") == 0) return expr_elem_size(cg, e->r);
            return expr_elem_size(cg, e->r);
        case EXPR_BIN:
            if (expr_is_pointer(cg, e->l)) return expr_elem_size(cg, e->l);
            if (expr_is_pointer(cg, e->r)) return expr_elem_size(cg, e->r);
            return expr_size(cg, e);
        case EXPR_ASSIGN: return expr_elem_size(cg, e->l);
        case EXPR_INCDEC: return expr_elem_size(cg, e->r ? e->r : e->l);
        default: return expr_size(cg, e);
    }
}

static int expr_elem_unsigned(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_VAR:
            if (var_is_array(cg, e)) return var_unsigned(cg, e);
            if (var_ptr_depth(cg, e) > 0) return var_unsigned(cg, e);
            return var_unsigned(cg, e);
        case EXPR_STR: return 0;
        case EXPR_SIZEOF: return 0;
        case EXPR_COND: return expr_unsigned(cg, e->r) || expr_unsigned(cg, e->c);
        case EXPR_COMMA: return expr_unsigned(cg, e->r);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            return m ? m->is_unsigned : 0;
        }
        case EXPR_INDEX:
            return expr_elem_unsigned(cg, e->l);
        case EXPR_UNARY:
            if (strcmp(e->op, "&") == 0) return 1;
            if (strcmp(e->op, "*") == 0) return expr_elem_unsigned(cg, e->r);
            return expr_elem_unsigned(cg, e->r);
        default: return expr_unsigned(cg, e);
    }
}

static int expr_elem_is_float(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_VAR: return var_elem_is_float(cg, e);
        case EXPR_INDEX:
            return expr_elem_is_float(cg, e->l);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            return m ? m->is_float : 0;
        }
        case EXPR_UNARY:
            if (strcmp(e->op, "*") == 0) return expr_elem_is_float(cg, e->r);
            if (strcmp(e->op, "&") == 0) return 0;
            return expr_elem_is_float(cg, e->r);
        default: return expr_is_float(cg, e);
    }
}

static int expr_elem_is_double(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_VAR: return var_elem_is_double(cg, e);
        case EXPR_INDEX:
            return expr_elem_is_double(cg, e->l);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            return m ? m->is_double : 0;
        }
        case EXPR_UNARY:
            if (strcmp(e->op, "*") == 0) return expr_elem_is_double(cg, e->r);
            if (strcmp(e->op, "&") == 0) return 0;
            return expr_elem_is_double(cg, e->r);
        default: return expr_is_double(cg, e);
    }
}

static int expr_elem_is_bool(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_VAR: return var_elem_is_bool(cg, e);
        case EXPR_INDEX:
            return expr_elem_is_bool(cg, e->l);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            return m ? m->is_bool : 0;
        }
        case EXPR_UNARY:
            if (strcmp(e->op, "*") == 0) return expr_elem_is_bool(cg, e->r);
            if (strcmp(e->op, "&") == 0) return 0;
            return expr_elem_is_bool(cg, e->r);
        default: return expr_is_bool(cg, e);
    }
}

static void gen_lvalue_addr(CodeGen *cg, Expr *e) {
    if (!e) { fprintf(stderr, "空左值\n"); exit(1); }
    if (e->kind == EXPR_VAR) {
        if (!local_info(cg, e->name) && !param_info(cg, e->name) &&
            !global_exists(cg->prog, e->name) && func_exists(cg->prog, e->name)) {
            cg_emit(cg, "LET A, DWORD func_%s", e->name);
            return;
        }
        var_addr(cg, e);
        return;
    }
    if (e->kind == EXPR_UNARY && strcmp(e->op, "*") == 0) {
        gen_expr(cg, e->r);
        return;
    }
    if (e->kind == EXPR_INDEX) {
        gen_expr(cg, e->l);            /* 数组名/指针值 → 基址 */
        cg_emit(cg, "PUSH DWORD A");
        gen_expr(cg, e->r);            /* 下标 */
        int esz = expr_elem_size(cg, e->l);
        if (esz == 2) cg_emit(cg, "SHL DWORD A, 1");
        else if (esz == 4) cg_emit(cg, "SHL DWORD A, 2");
        else if (esz == 8) cg_emit(cg, "SHL DWORD A, 3");
        else if (esz != 1) {
            cg_emit(cg, "LET R, DWORD %d", esz);
            cg_emit(cg, "MUL DWORD A, R");
            cg_emit(cg, "MOV A, D2");
        }
        cg_emit(cg, "POP DWORD B");
        cg_emit(cg, "ADD DWORD A, B");
        return;
    }
    if (e->kind == EXPR_MEMBER) {
        if (e->arrow) gen_expr(cg, e->l);
        else gen_lvalue_addr(cg, e->l);
        int off = member_offset(cg, expr_struct_name(cg, e->l), e->member);
        if (off != 0) cg_emit(cg, "ADD DWORD A, %d", off);
        return;
    }
    if (e->kind == EXPR_REGDIR) {
        fprintf(stderr, "line %d: 不能对寄存器取地址 __reg_%s\n", e->line, e->name ? e->name : "?");
        exit(1);
    }
    fprintf(stderr, "line %d: 不是可寻址的左值\n", e->line);
    exit(1);
}

static void gen_incdec(CodeGen *cg, Expr *e, int postfix) {
    Expr *target = e->r ? e->r : e->l;
    if (var_is_const(cg, target)) {
        fprintf(stderr, "line %d: 不能修改 const 变量 %s\n", target->line, target->name ? target->name : "?");
        exit(1);
    }
    if (target->kind == EXPR_REGDIR) {
        if (strcmp(target->name, "E") == 0) {
            fprintf(stderr, "line %d: __reg_E 只能作为右值，不能自增/自减\n", target->line);
            exit(1);
        }
        cg_emit(cg, "MOV A, %s", target->name);
        if (postfix) cg_emit(cg, "PUSH DWORD A");
        if (strcmp(e->op, "+") == 0) cg_emit(cg, "ADD DWORD A, 1");
        else cg_emit(cg, "SUB DWORD A, 1");
        cg_emit(cg, "MOV %s, A", target->name);
        if (postfix) cg_emit(cg, "POP DWORD A");
        return;
    }
    gen_lvalue_addr(cg, target);
    cg_emit(cg, "MOV B, A");
    int sz = expr_size(cg, target);
    int step = expr_is_pointer(cg, target) ? expr_elem_size(cg, target) : 1;
    emit_load_from_b(cg, sz, expr_unsigned(cg, target));
    if (postfix) cg_emit(cg, "PUSH DWORD A");
    if (strcmp(e->op, "+") == 0) {
        if (step == 1) cg_emit(cg, "ADD DWORD A, 1");
        else if (step == 2) cg_emit(cg, "ADD DWORD A, 2");
        else if (step == 4) cg_emit(cg, "ADD DWORD A, 4");
        else if (step == 8) cg_emit(cg, "ADD DWORD A, 8");
        else cg_emit(cg, "ADD DWORD A, %d", step);
    } else {
        if (step == 1) cg_emit(cg, "SUB DWORD A, 1");
        else if (step == 2) cg_emit(cg, "SUB DWORD A, 2");
        else if (step == 4) cg_emit(cg, "SUB DWORD A, 4");
        else if (step == 8) cg_emit(cg, "SUB DWORD A, 8");
        else cg_emit(cg, "SUB DWORD A, %d", step);
    }
    emit_store_to_b(cg, sz);
    if (postfix) cg_emit(cg, "POP DWORD A");
}

static void gen_push_struct_arg(CodeGen *cg, Expr *arg, int *argbytes) {
    int sz = expr_size(cg, arg);
    gen_expr(cg, arg);          /* A = 结构体地址 */
    cg_emit(cg, "MOV R, A");
    cg_emit(cg, "LET C, DWORD %d", sz);
    const char *lp = cg_new_label(cg, "PS");
    cg_emit(cg, "%s:", lp);
    cg_emit(cg, "LOD BYTE A");
    cg_emit(cg, "PUSH BYTE A");
    cg_emit(cg, "CDI");
    cg_emit(cg, "LET E, DWORD %s", lp);
    cg_emit(cg, "JNZ");
    *argbytes += sz;
}

static void gen_call(CodeGen *cg, Expr *e) {
    if (e->name && strcmp(e->name, "__builtin_va_start") == 0) {
        if (e->nargs < 1) { fprintf(stderr, "__builtin_va_start 缺少参数\n"); exit(1); }
        gen_lvalue_addr(cg, e->args[0]);
        cg_emit(cg, "MOV B, A");
        cg_emit(cg, "MOV A, F");
        cg_emit(cg, "SUB DWORD A, %d", cg->cur_func_named_bytes + 11);
        cg_emit(cg, "ST DWORD *B, A");
        return;
    }
    if (e->name && strcmp(e->name, "__builtin_va_arg") == 0) {
        if (e->nargs < 2 || e->args[1]->kind != EXPR_NUM) {
            fprintf(stderr, "__builtin_va_arg 缺少类型尺寸\n");
            exit(1);
        }
        int sz = (int)e->args[1]->ival;
        gen_lvalue_addr(cg, e->args[0]);
        cg_emit(cg, "PUSH DWORD A");          /* [&ap] */
        cg_emit(cg, "MOV B, A");
        cg_emit(cg, "LR DWORD A, *B");        /* A = old ap */
        cg_emit(cg, "PUSH DWORD A");          /* [&ap][old] */
        cg_emit(cg, "LET A, DWORD 8");
        cg_emit(cg, "POP DWORD B");           /* B = old */
        cg_emit(cg, "PUSH DWORD B");          /* [&ap][old] */
        cg_emit(cg, "SUB DWORD B, 8");        /* B = new ap */
        cg_emit(cg, "MOV A, B");
        cg_emit(cg, "POP DWORD C");           /* C = old */
        cg_emit(cg, "PUSH DWORD A");          /* [&ap][new] */
        cg_emit(cg, "MOV A, C");              /* A = old */
        if (sz == 8) {
            cg_emit(cg, "LR DWORD A, *A");
            cg_emit(cg, "PUSH DWORD A");
            cg_emit(cg, "MOV A, C");
            cg_emit(cg, "ADD DWORD A, 4");
            cg_emit(cg, "LR DWORD A, *A");
            cg_emit(cg, "MOV D1, A");
            cg_emit(cg, "POP DWORD A");
        } else {
            cg_emit(cg, "LR DWORD A, *A");
        }
        cg_emit(cg, "POP DWORD C");           /* C = new ap */
        cg_emit(cg, "POP DWORD B");           /* B = &ap */
        cg_emit(cg, "ST DWORD *B, C");
        return;
    }
    int direct = e->name && func_exists(cg->prog, e->name);
    if (!direct && !e->l) {
        fprintf(stderr, "line %d: 未定义函数 %s\n", e->line, e->name ? e->name : "?");
        exit(1);
    }
    int argbytes = 0;
    cg_emit(cg, "MOV A, F");
    cg_emit(cg, "PUSH DWORD A");
    int fn_vararg = 0, named_count = 0;
    if (direct) {
        for (int i = 0; i < cg->prog->nfuncs; i++) {
            if (strcmp(cg->prog->funcs[i].name, e->name) == 0) {
                fn_vararg = cg->prog->funcs[i].is_vararg;
                named_count = cg->prog->funcs[i].nparams;
                break;
            }
        }
    }
    if (fn_vararg) {
        for (int i = e->nargs - 1; i >= named_count; i--) {
            gen_expr(cg, e->args[i]);
            if (expr_is_float(cg, e->args[i]) || expr_is_double(cg, e->args[i]))
                emit_conv_to_int(cg, e->args[i]);
            if (expr_size(cg, e->args[i]) != 8) {
                if (expr_unsigned(cg, e->args[i])) cg_emit(cg, "ZERO D1");
                else cg_emit(cg, "MSR DWORD D1, 31");
            }
            cg_emit(cg, "PUSH DWORD A");
            cg_emit(cg, "PUSH DWORD D1");
            argbytes += 8;
        }
        for (int i = 0; i < named_count && i < e->nargs; i++) {
            if (expr_is_struct(cg, e->args[i])) {
                gen_push_struct_arg(cg, e->args[i], &argbytes);
            } else {
                gen_expr(cg, e->args[i]);
                if (expr_size(cg, e->args[i]) == 10) {
                    cg_emit(cg, "EPUSH EP0");
                    argbytes += 10;
                } else if (expr_is_float(cg, e->args[i])) {
                    cg_emit(cg, "FPUSH FP0");
                    argbytes += 4;
                } else if (expr_is_double(cg, e->args[i])) {
                    cg_emit(cg, "DPUSH DP0");
                    argbytes += 8;
                } else if (expr_size(cg, e->args[i]) == 8) {
                    cg_emit(cg, "PUSH DWORD A");
                    cg_emit(cg, "PUSH DWORD D1");
                    argbytes += 8;
                } else {
                    cg_emit(cg, "PUSH DWORD A");
                    argbytes += 4;
                }
            }
        }
    } else {
        for (int i = 0; i < e->nargs; i++) {
            if (expr_is_struct(cg, e->args[i])) {
                gen_push_struct_arg(cg, e->args[i], &argbytes);
            } else {
                gen_expr(cg, e->args[i]);
                if (expr_size(cg, e->args[i]) == 10) {
                    cg_emit(cg, "EPUSH EP0");
                    argbytes += 10;
                } else if (expr_is_float(cg, e->args[i])) {
                    cg_emit(cg, "FPUSH FP0");
                    argbytes += 4;
                } else if (expr_is_double(cg, e->args[i])) {
                    cg_emit(cg, "DPUSH DP0");
                    argbytes += 8;
                } else if (expr_size(cg, e->args[i]) == 8) {
                    cg_emit(cg, "PUSH DWORD A");
                    cg_emit(cg, "PUSH DWORD D1");
                    argbytes += 8;
                } else {
                    cg_emit(cg, "PUSH DWORD A");
                    argbytes += 4;
                }
            }
        }
    }

    const char *ret = cg_new_label(cg, "RET");
    if (direct) {
        cg_emit(cg, "LET E, DWORD %s", ret);
        cg_emit(cg, "PUSH DWORD E");
        cg_emit(cg, "LET E, DWORD func_%s", e->name);
        cg_emit(cg, "JMP");
    } else {
        gen_expr(cg, e->l);          /* A = 函数指针 */
        cg_emit(cg, "MOV E, A");
        cg_emit(cg, "LET B, DWORD %s", ret);
        cg_emit(cg, "PUSH DWORD B");
        cg_emit(cg, "JMP");
    }
    cg_emit(cg, "%s:", ret);
    if (argbytes) cg_emit(cg, "SUB DWORD S, %d", argbytes);
    cg_emit(cg, "POP DWORD F");
    int rsz = 4, rf = 0, rd = 0, ris = 0;
    if (direct) {
        for (int i = 0; i < cg->prog->nfuncs; i++)
            if (strcmp(cg->prog->funcs[i].name, e->name) == 0) {
                rsz = cg->prog->funcs[i].ret_size;
                rf = cg->prog->funcs[i].ret_float;
                rd = cg->prog->funcs[i].ret_double;
                ris = cg->prog->funcs[i].ret_is_struct && cg->prog->funcs[i].ret_ptr_depth == 0;
            }
    } else {
        int rvoid = 0;
        const char *rsname = NULL;
        var_func_ret_info(cg, e->l, &rsz, &rf, &rd, &rvoid, &ris, &rsname);
        (void)rsname;
    }
    if (ris)
        cg_emit(cg, "LET A, DWORD struct_ret");
    else if (rsz != 8 && !rf && !rd)
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


static void emit_conv_to_float(CodeGen *cg, Expr *e) {
    if (expr_size(cg, e) == 10) cg_emit(cg, "E2F FP0, EP0");
    else if (expr_is_double(cg, e)) cg_emit(cg, "D2F FP0, DP0");
    else if (!expr_is_float(cg, e)) cg_emit(cg, "I2F FP0, A");
}

static void emit_conv_to_double(CodeGen *cg, Expr *e) {
    if (expr_size(cg, e) == 10) cg_emit(cg, "E2D DP0, EP0");
    else if (expr_is_float(cg, e)) cg_emit(cg, "F2D DP0, FP0");
    else if (!expr_is_double(cg, e)) cg_emit(cg, "I2D DP0, A");
}

static void emit_conv_to_long_double(CodeGen *cg, Expr *e) {
    if (expr_size(cg, e) == 10) return;
    if (expr_is_float(cg, e)) cg_emit(cg, "F2E EP0, FP0");
    else if (expr_is_double(cg, e)) cg_emit(cg, "D2E EP0, DP0");
    else cg_emit(cg, "I2E EP0, A");
}

static void emit_conv_to_int(CodeGen *cg, Expr *e) {
    if (expr_size(cg, e) == 10) cg_emit(cg, "E2I A, EP0");
    else if (expr_is_float(cg, e)) cg_emit(cg, "F2I A, FP0");
    else if (expr_is_double(cg, e)) cg_emit(cg, "D2I A, DP0");
}

static void gen_fp_binop(CodeGen *cg, Expr *e) {
    const char *op = e->op;
    int is_long = expr_size(cg, e->l) == 10 || expr_size(cg, e->r) == 10;
    int is_double = expr_is_double(cg, e->l) || expr_is_double(cg, e->r);
    /* 右操作数 → FP1/DP1/EP1 */
    gen_expr(cg, e->r);
    if (is_long) {
        emit_conv_to_long_double(cg, e->r);
        cg_emit(cg, "EMOV EP1, EP0");
    } else if (is_double) {
        emit_conv_to_double(cg, e->r);
        cg_emit(cg, "DMOV DP1, DP0");
    } else {
        emit_conv_to_float(cg, e->r);
        cg_emit(cg, "FMOV FP1, FP0");
    }
    /* 左操作数 → FP0/DP0/EP0 */
    gen_expr(cg, e->l);
    if (is_long) {
        emit_conv_to_long_double(cg, e->l);
    } else if (is_double) {
        emit_conv_to_double(cg, e->l);
    } else {
        emit_conv_to_float(cg, e->l);
    }
    if (strcmp(op,"==")==0 || strcmp(op,"!=")==0 || strcmp(op,"<")==0 ||
        strcmp(op,"<=")==0 || strcmp(op,">")==0 || strcmp(op,">=")==0) {
        if (is_long) cg_emit(cg, "ECMP EP0, EP1");
        else cg_emit(cg, is_double ? "DCMP DP0, DP1" : "FCMP FP0, FP1");
        const char *lt = cg_new_label(cg, "FC");
        const char *le = cg_new_label(cg, "FC");
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
        return;
    }
    if (is_long) {
        if (strcmp(op,"+")==0) cg_emit(cg, "EADD EP0, EP1");
        else if (strcmp(op,"-")==0) cg_emit(cg, "ESUB EP0, EP1");
        else if (strcmp(op,"*")==0) cg_emit(cg, "EMUL EP0, EP1");
        else if (strcmp(op,"/")==0) cg_emit(cg, "EDIV EP0, EP1");
        else { fprintf(stderr, "不支持的浮点运算: %s\n", op); exit(1); }
    } else if (strcmp(op,"+")==0) cg_emit(cg, is_double ? "DADD DP0, DP1" : "FADD FP0, FP1");
    else if (strcmp(op,"-")==0) cg_emit(cg, is_double ? "DSUB DP0, DP1" : "FSUB FP0, FP1");
    else if (strcmp(op,"*")==0) cg_emit(cg, is_double ? "DMUL DP0, DP1" : "FMUL FP0, FP1");
    else if (strcmp(op,"/")==0) cg_emit(cg, is_double ? "DDIV DP0, DP1" : "FDIV FP0, FP1");
    else { fprintf(stderr, "不支持的浮点运算: %s\n", op); exit(1); }
}

static void gen_binop(CodeGen *cg, Expr *e) {
    const char *op = e->op;
    int has_fp = expr_is_float(cg, e->l) || expr_is_double(cg, e->l) ||
                 expr_is_float(cg, e->r) || expr_is_double(cg, e->r);
    if (strcmp(op, "&&") != 0 && strcmp(op, "||") != 0 && has_fp) {
        gen_fp_binop(cg, e);
        return;
    }
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
    /* 指针 ± 整数 */
    if ((strcmp(op, "+") == 0 || strcmp(op, "-") == 0) &&
        (expr_is_pointer(cg, e->l) || expr_is_pointer(cg, e->r))) {
        gen_expr(cg, e->r);
        cg_emit(cg, "PUSH DWORD A");
        gen_expr(cg, e->l);
        cg_emit(cg, "POP DWORD B");       /* B = 右操作数 */
        if (expr_is_pointer(cg, e->l)) {
            int esz = expr_elem_size(cg, e->l);
            if (esz == 2) cg_emit(cg, "SHL DWORD B, 1");
            else if (esz == 4) cg_emit(cg, "SHL DWORD B, 2");
            else if (esz == 8) cg_emit(cg, "SHL DWORD B, 3");
            else if (esz != 1) {
                cg_emit(cg, "LET R, DWORD %d", esz);
                cg_emit(cg, "MUL DWORD B, R");
                cg_emit(cg, "MOV B, D2");
            }
            if (strcmp(op, "-") == 0) cg_emit(cg, "SUB DWORD A, B");
            else cg_emit(cg, "ADD DWORD A, B");
        } else {
            int esz = expr_elem_size(cg, e->r);
            if (esz == 2) cg_emit(cg, "SHL DWORD A, 1");
            else if (esz == 4) cg_emit(cg, "SHL DWORD A, 2");
            else if (esz == 8) cg_emit(cg, "SHL DWORD A, 3");
            else if (esz != 1) {
                cg_emit(cg, "LET R, DWORD %d", esz);
                cg_emit(cg, "MUL DWORD A, R");
                cg_emit(cg, "MOV A, D2");
            }
            cg_emit(cg, "ADD DWORD A, B");
        }
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
    if (var_is_const(cg, e->l)) {
        fprintf(stderr, "line %d: 不能修改 const 变量 %s\n", e->l->line, e->l->name ? e->l->name : "?");
        exit(1);
    }
    if (e->l->kind == EXPR_REGDIR) {
        if (strcmp(e->l->name, "E") == 0) {
            fprintf(stderr, "line %d: __reg_E 只能作为右值，不能赋值\n", e->l->line);
            exit(1);
        }
        if (strcmp(e->op, "=") == 0) {
            gen_expr(cg, e->r);
            cg_emit(cg, "MOV %s, A", e->l->name);
            return;
        }
        cg_emit(cg, "MOV A, %s", e->l->name);
        cg_emit(cg, "PUSH DWORD A");
        gen_expr(cg, e->r);
        cg_emit(cg, "POP DWORD B");
        const char *op = e->op;
        if (strcmp(op, "+=")==0) { cg_emit(cg, "ADD DWORD B, A"); cg_emit(cg, "MOV A, B"); }
        else if (strcmp(op, "-=")==0) { cg_emit(cg, "SUB DWORD B, A"); cg_emit(cg, "MOV A, B"); }
        else if (strcmp(op, "*=")==0) { cg_emit(cg, "MUL DWORD B, A"); cg_emit(cg, "MOV A, D2"); }
        else if (strcmp(op, "/=")==0) { cg_emit(cg, "DIV DWORD B, A"); cg_emit(cg, "MOV A, D2"); }
        else if (strcmp(op, "%=")==0) { cg_emit(cg, "DIV DWORD B, A"); cg_emit(cg, "MOV A, D1"); }
        else if (strcmp(op, "&=")==0) { cg_emit(cg, "AND DWORD B, A"); cg_emit(cg, "MOV A, B"); }
        else if (strcmp(op, "|=")==0) { cg_emit(cg, "OR DWORD B, A"); cg_emit(cg, "MOV A, B"); }
        else if (strcmp(op, "^=")==0) { cg_emit(cg, "XOR DWORD B, A"); cg_emit(cg, "MOV A, B"); }
        else if (strcmp(op, "<<=")==0) { cg_emit(cg, "SHL DWORD B, A"); cg_emit(cg, "MOV A, B"); }
        else if (strcmp(op, ">>=")==0) { cg_emit(cg, "SHR DWORD B, A"); cg_emit(cg, "MOV A, B"); }
        else { fprintf(stderr, "不支持的寄存器复合赋值: %s\n", op); exit(1); }
        cg_emit(cg, "MOV %s, A", e->l->name);
        return;
    }
    if (expr_is_struct(cg, e->l)) {
        if (strcmp(e->op, "=") != 0) {
            fprintf(stderr, "line %d: 结构体不支持复合赋值\n", e->line);
            exit(1);
        }
        int sz = expr_size(cg, e->l);
        gen_lvalue_addr(cg, e->l);
        cg_emit(cg, "PUSH DWORD A");
        gen_expr(cg, e->r);
        cg_emit(cg, "MOV R, A");
        cg_emit(cg, "POP DWORD B");
        cg_emit(cg, "LET C, DWORD %d", sz);
        const char *lp = cg_new_label(cg, "ST");
        cg_emit(cg, "%s:", lp);
        cg_emit(cg, "LOD BYTE A");
        cg_emit(cg, "ST BYTE *B, A");
        cg_emit(cg, "INC B");
        cg_emit(cg, "CDI");
        cg_emit(cg, "LET E, DWORD %s", lp);
        cg_emit(cg, "JNZ");
        return;
    }
    if (e->l->kind == EXPR_MEMBER) {
        MemberDef *lm = expr_member_def(cg, e->l);
        if (lm && lm->is_bitfield) {
            if (strcmp(e->op, "=") != 0) {
                fprintf(stderr, "line %d: 位域暂不支持复合赋值\n", e->line);
                exit(1);
            }
            gen_expr(cg, e->r);
            gen_lvalue_addr(cg, e->l);
            emit_store_bitfield(cg, lm);
            return;
        }
    }
    int lhs_size = expr_size(cg, e->l);
    int lhs_float = expr_is_float(cg, e->l);
    int lhs_double = expr_is_double(cg, e->l);
    int lhs_bool = expr_is_bool(cg, e->l);

    gen_expr(cg, e->r);
    if (lhs_size == 10 && lhs_double) {
        emit_conv_to_long_double(cg, e->r);
        gen_lvalue_addr(cg, e->l);
        cg_emit(cg, "MOV B, A");
        cg_emit(cg, "EST *B, EP0");
        return;
    }
    if (lhs_float) {
        emit_conv_to_float(cg, e->r);
        gen_lvalue_addr(cg, e->l);
        cg_emit(cg, "MOV B, A");
        cg_emit(cg, "FST *B, FP0");
        return;
    }
    if (lhs_double) {
        emit_conv_to_double(cg, e->r);
        gen_lvalue_addr(cg, e->l);
        cg_emit(cg, "MOV B, A");
        cg_emit(cg, "DST *B, DP0");
        return;
    }
    if (expr_is_float(cg, e->r) || expr_is_double(cg, e->r)) {
        emit_conv_to_int(cg, e->r);
    }
    if (lhs_bool) {
        emit_bool_normalize(cg);
    }
    cg_emit(cg, "PUSH DWORD A");
    gen_lvalue_addr(cg, e->l);
    cg_emit(cg, "MOV B, A");
    cg_emit(cg, "POP DWORD A");
    if (strcmp(e->op, "=") == 0) {
        emit_store_to_b(cg, lhs_size);
        return;
    }
    /* 复合赋值 */
    cg_emit(cg, "PUSH DWORD B");      /* [addr] */
    cg_emit(cg, "PUSH DWORD A");      /* [addr][rhs] */
    emit_load_from_b(cg, lhs_size, expr_unsigned(cg, e->l));
    cg_emit(cg, "MOV C, A");
    cg_emit(cg, "POP DWORD B");       /* B = rhs */
    const char *op = e->op;
    if (strcmp(op, "+=")==0) {
        if (expr_is_pointer(cg, e->l)) {
            int esz = expr_elem_size(cg, e->l);
            if (esz == 2) cg_emit(cg, "SHL DWORD B, 1");
            else if (esz == 4) cg_emit(cg, "SHL DWORD B, 2");
            else if (esz == 8) cg_emit(cg, "SHL DWORD B, 3");
            else if (esz != 1) {
                cg_emit(cg, "LET R, DWORD %d", esz);
                cg_emit(cg, "MUL DWORD B, R");
                cg_emit(cg, "MOV B, D2");
            }
            cg_emit(cg, "ADD DWORD C, B");
        } else cg_emit(cg, "ADD DWORD C, B");
    }
    else if (strcmp(op, "-=")==0) {
        if (expr_is_pointer(cg, e->l)) {
            int esz = expr_elem_size(cg, e->l);
            if (esz == 2) cg_emit(cg, "SHL DWORD B, 1");
            else if (esz == 4) cg_emit(cg, "SHL DWORD B, 2");
            else if (esz == 8) cg_emit(cg, "SHL DWORD B, 3");
            else if (esz != 1) {
                cg_emit(cg, "LET R, DWORD %d", esz);
                cg_emit(cg, "MUL DWORD B, R");
                cg_emit(cg, "MOV B, D2");
            }
            cg_emit(cg, "SUB DWORD C, B");
        } else cg_emit(cg, "SUB DWORD C, B");
    }
    else if (strcmp(op, "*=")==0) { cg_emit(cg, "MUL DWORD C, B"); cg_emit(cg, "MOV C, D2"); }
    else if (strcmp(op, "/=")==0) { cg_emit(cg, "DIV DWORD C, B"); cg_emit(cg, "MOV C, D2"); }
    else if (strcmp(op, "%=")==0) { cg_emit(cg, "DIV DWORD C, B"); cg_emit(cg, "MOV C, D1"); }
    else if (strcmp(op, "&=")==0) cg_emit(cg, "AND DWORD C, B");
    else if (strcmp(op, "|=")==0) cg_emit(cg, "OR DWORD C, B");
    else if (strcmp(op, "^=")==0) cg_emit(cg, "XOR DWORD C, B");
    else if (strcmp(op, "<<=")==0) cg_emit(cg, "SHL DWORD C, B");
    else if (strcmp(op, ">>=")==0) cg_emit(cg, (expr_unsigned(cg, e->l) ? "SHR DWORD C, B" : "MSR DWORD C, B"));
    cg_emit(cg, "POP DWORD B");
    cg_emit(cg, "MOV A, C");
    emit_store_to_b(cg, lhs_size);
}

static int expr_size(CodeGen *cg, Expr *e) {
    if (!e) return 4;
    switch (e->kind) {
        case EXPR_NUM: return e->type_size;
        case EXPR_REGDIR: return 4;
        case EXPR_STR: return 4;
        case EXPR_SIZEOF: return 4;
        case EXPR_COND: return expr_size(cg, e->r);
        case EXPR_COMMA: return expr_size(cg, e->r);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            if (!m) return 4;
            if (m->is_array) return 4;
            if (m->ptr_depth > 0) return 4;
            return m->size;
        }
        case EXPR_VAR:
            if (var_is_array(cg, e) || var_ptr_depth(cg, e) > 0) return 4;
            return var_size(cg, e);
        case EXPR_INDEX:
            if (expr_is_array(cg, e)) return 4;
            return expr_elem_size(cg, e);
        case EXPR_BIN: {
            if ((strcmp(e->op, "+") == 0 || strcmp(e->op, "-") == 0) &&
                (expr_is_pointer(cg, e->l) || expr_is_pointer(cg, e->r))) return 4;
            if (strcmp(e->op,"==")==0 || strcmp(e->op,"!=")==0 || strcmp(e->op,"<")==0 ||
                strcmp(e->op,"<=")==0 || strcmp(e->op,">")==0 || strcmp(e->op,">=")==0 ||
                strcmp(e->op,"&&")==0 || strcmp(e->op,"||")==0) return 4;
            int l = expr_size(cg, e->l), r = expr_size(cg, e->r);
            if (l == 10 || r == 10) return 10;
            return (l == 8 || r == 8) ? 8 : 4;
        }
        case EXPR_UNARY:
            if (strcmp(e->op, "&") == 0) return 4;
            if (strcmp(e->op, "*") == 0) return expr_elem_size(cg, e->r);
            return expr_size(cg, e->r);
        case EXPR_ASSIGN: return expr_size(cg, e->l);
        case EXPR_INCDEC: return expr_size(cg, e->r ? e->r : e->l);
        case EXPR_CAST: return e->is_double ? (e->type_size == 10 ? 10 : 8) : (e->is_float ? 4 : (e->type_size > 0 ? e->type_size : 4));
        case EXPR_CALL: {
            if (e->name) {
                for (int i = 0; i < cg->prog->nfuncs; i++) {
                    if (strcmp(cg->prog->funcs[i].name, e->name) == 0)
                        return cg->prog->funcs[i].ret_size;
                }
            }
            return 4;
        }
        default: return 4;
    }
}

static int expr_unsigned(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_NUM: return e->is_unsigned;
        case EXPR_REGDIR: return 1;
        case EXPR_STR: return 1;
        case EXPR_SIZEOF: return 0;
        case EXPR_COND: return expr_unsigned(cg, e->r) || expr_unsigned(cg, e->c);
        case EXPR_COMMA: return expr_unsigned(cg, e->r);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            if (!m) return 0;
            if (m->is_array || m->ptr_depth > 0) return 1;
            return m->is_unsigned;
        }
        case EXPR_VAR:
            if (var_is_array(cg, e) || var_ptr_depth(cg, e) > 0) return 1;
            return var_unsigned(cg, e);
        case EXPR_INDEX:
            if (expr_is_array(cg, e)) return 1;
            return expr_elem_unsigned(cg, e->l);
        case EXPR_BIN: {
            if ((strcmp(e->op, "+") == 0 || strcmp(e->op, "-") == 0) &&
                (expr_is_pointer(cg, e->l) || expr_is_pointer(cg, e->r))) return 1;
            if (strcmp(e->op,"==")==0 || strcmp(e->op,"!=")==0 || strcmp(e->op,"<")==0 ||
                strcmp(e->op,"<=")==0 || strcmp(e->op,">")==0 || strcmp(e->op,">=")==0) {
                if (expr_is_pointer(cg, e->l) || expr_is_pointer(cg, e->r)) return 1;
                return 0;
            }
            int l = expr_unsigned(cg, e->l), r = expr_unsigned(cg, e->r);
            int ls = expr_size(cg, e->l), rs = expr_size(cg, e->r);
            if (ls != rs) return ls > rs ? l : r;
            return l || r;
        }
        case EXPR_UNARY:
            if (strcmp(e->op, "&") == 0) return 1;
            if (strcmp(e->op, "*") == 0) return expr_elem_unsigned(cg, e->r);
            return expr_unsigned(cg, e->r);
        case EXPR_ASSIGN: return expr_unsigned(cg, e->l);
        case EXPR_INCDEC: return expr_unsigned(cg, e->r ? e->r : e->l);
        case EXPR_CAST: return e->is_unsigned;
        default: return 0;
    }
}

static int expr_is_float(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_NUM: return e->is_float;
        case EXPR_REGDIR: return 0;
        case EXPR_STR: return 0;
        case EXPR_SIZEOF: return 0;
        case EXPR_COND: return expr_unsigned(cg, e->r) || expr_unsigned(cg, e->c);
        case EXPR_COMMA: return expr_unsigned(cg, e->r);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            return m && !m->is_array && m->ptr_depth == 0 && m->is_float;
        }
        case EXPR_VAR: return var_is_float(cg, e);
        case EXPR_INDEX:
            if (expr_is_array(cg, e)) return 0;
            return expr_elem_is_float(cg, e->l);
        case EXPR_BIN:
            if (strcmp(e->op,"==")==0 || strcmp(e->op,"!=")==0 || strcmp(e->op,"<")==0 ||
                strcmp(e->op,"<=")==0 || strcmp(e->op,">")==0 || strcmp(e->op,">=")==0 ||
                strcmp(e->op,"&&")==0 || strcmp(e->op,"||")==0) return 0;
            return expr_is_float(cg, e->l) || expr_is_float(cg, e->r);
        case EXPR_UNARY:
            if (strcmp(e->op, "*") == 0) return expr_elem_is_float(cg, e->r);
            if (strcmp(e->op, "&") == 0) return 0;
            return expr_is_float(cg, e->r);
        case EXPR_ASSIGN: return expr_is_float(cg, e->l);
        case EXPR_INCDEC: return expr_is_float(cg, e->r ? e->r : e->l);
        case EXPR_CAST: return e->is_float;
        case EXPR_CALL: {
            if (e->name) {
                for (int i = 0; i < cg->prog->nfuncs; i++) {
                    if (strcmp(cg->prog->funcs[i].name, e->name) == 0)
                        return cg->prog->funcs[i].ret_float && cg->prog->funcs[i].ret_ptr_depth == 0;
                }
            }
            return 0;
        }
        default: return 0;
    }
}

static int expr_is_double(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_NUM: return e->is_double;
        case EXPR_REGDIR: return 0;
        case EXPR_STR: return 0;
        case EXPR_SIZEOF: return 0;
        case EXPR_COND: return expr_unsigned(cg, e->r) || expr_unsigned(cg, e->c);
        case EXPR_COMMA: return expr_unsigned(cg, e->r);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            return m && !m->is_array && m->ptr_depth == 0 && m->is_double;
        }
        case EXPR_VAR: return var_is_double(cg, e);
        case EXPR_INDEX:
            if (expr_is_array(cg, e)) return 0;
            return expr_elem_is_double(cg, e->l);
        case EXPR_BIN:
            if (strcmp(e->op,"==")==0 || strcmp(e->op,"!=")==0 || strcmp(e->op,"<")==0 ||
                strcmp(e->op,"<=")==0 || strcmp(e->op,">")==0 || strcmp(e->op,">=")==0 ||
                strcmp(e->op,"&&")==0 || strcmp(e->op,"||")==0) return 0;
            return expr_is_double(cg, e->l) || expr_is_double(cg, e->r);
        case EXPR_UNARY:
            if (strcmp(e->op, "*") == 0) return expr_elem_is_double(cg, e->r);
            if (strcmp(e->op, "&") == 0) return 0;
            return expr_is_double(cg, e->r);
        case EXPR_ASSIGN: return expr_is_double(cg, e->l);
        case EXPR_INCDEC: return expr_is_double(cg, e->r ? e->r : e->l);
        case EXPR_CAST: return e->is_double;
        case EXPR_CALL: {
            if (e->name) {
                for (int i = 0; i < cg->prog->nfuncs; i++) {
                    if (strcmp(cg->prog->funcs[i].name, e->name) == 0)
                        return cg->prog->funcs[i].ret_double && cg->prog->funcs[i].ret_ptr_depth == 0;
                }
            }
            return 0;
        }
        default: return 0;
    }
}

static int expr_is_bool(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_REGDIR: return 0;
        case EXPR_STR: return 0;
        case EXPR_SIZEOF: return 0;
        case EXPR_COND: return expr_unsigned(cg, e->r) || expr_unsigned(cg, e->c);
        case EXPR_COMMA: return expr_unsigned(cg, e->r);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            return m && !m->is_array && m->ptr_depth == 0 && m->is_bool;
        }
        case EXPR_VAR: return var_is_bool(cg, e);
        case EXPR_INDEX:
            if (expr_is_array(cg, e)) return 0;
            return expr_elem_is_bool(cg, e->l);
        case EXPR_UNARY:
            if (strcmp(e->op, "*") == 0) return expr_elem_is_bool(cg, e->r);
            return 0;
        case EXPR_ASSIGN: return expr_is_bool(cg, e->l);
        case EXPR_INCDEC: return expr_is_bool(cg, e->r ? e->r : e->l);
        default: return 0;
    }
}

static MemberDef *expr_member_def(CodeGen *cg, Expr *e) {
    if (!e || e->kind != EXPR_MEMBER) return NULL;
    const char *sname = expr_struct_name(cg, e->l);
    if (!sname) return NULL;
    return member_def(cg, sname, e->member);
}

static int expr_is_struct(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_VAR: return var_is_struct(cg, e);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            return m && m->is_struct && m->ptr_depth == 0 && !m->is_array;
        }
        case EXPR_CALL: {
            for (int i = 0; i < cg->prog->nfuncs; i++) {
                if (strcmp(cg->prog->funcs[i].name, e->name) == 0)
                    return cg->prog->funcs[i].ret_is_struct && cg->prog->funcs[i].ret_ptr_depth == 0;
            }
            return 0;
        }
        case EXPR_ASSIGN: return expr_is_struct(cg, e->l);
        case EXPR_INCDEC: return expr_is_struct(cg, e->r ? e->r : e->l);
        default: return 0;
    }
}

static const char *expr_struct_name(CodeGen *cg, Expr *e) {
    if (!e) return NULL;
    switch (e->kind) {
        case EXPR_VAR: return var_struct_name(cg, e);
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            return m ? m->struct_name : NULL;
        }
        case EXPR_CALL: {
            for (int i = 0; i < cg->prog->nfuncs; i++) {
                if (strcmp(cg->prog->funcs[i].name, e->name) == 0)
                    return cg->prog->funcs[i].ret_struct_name;
            }
            return NULL;
        }
        case EXPR_ASSIGN: return expr_struct_name(cg, e->l);
        case EXPR_INCDEC: return expr_struct_name(cg, e->r ? e->r : e->l);
        default: return NULL;
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

static void emit_load_from_a(CodeGen *cg, int size, int is_unsigned) {
    if (size == 8) {
        cg_emit(cg, "MOV R, A");
        cg_emit(cg, "LR DWORD A, *R");
        cg_emit(cg, "ADD DWORD R, 4");
        cg_emit(cg, "LR DWORD D1, *R");
    } else if (size == 1) {
        cg_emit(cg, "LR BYTE A, *A");
        if (!is_unsigned) {
            cg_emit(cg, "SHL DWORD A, 24");
            cg_emit(cg, "MSR DWORD A, 24");
        }
    } else if (size == 2) {
        cg_emit(cg, "LR WORD A, *A");
        if (!is_unsigned) {
            cg_emit(cg, "SHL DWORD A, 16");
            cg_emit(cg, "MSR DWORD A, 16");
        }
    } else {
        cg_emit(cg, "LR DWORD A, *A");
    }
}

static int sizeof_expr(CodeGen *cg, Expr *e) {
    if (!e) return 0;
    if (e->kind == EXPR_VAR && var_is_array(cg, e)) return var_size(cg, e);
    if (e->kind == EXPR_STR) return (int)strlen(e->name) + 1;
    return expr_size(cg, e);
}

static void gen_expr(CodeGen *cg, Expr *e) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_NUM:
            if (e->is_float) {
                uint32_t bits;
                float fv = (float)e->fval;
                memcpy(&bits, &fv, sizeof(bits));
                cg_emit(cg, "FLDI FP0, 0x%08X", bits);
            } else if (e->is_double && e->type_size == 10) {
                unsigned long long lo;
                unsigned int hi;
                double_to_ext80(e->fval, &lo, &hi);
                cg_emit(cg, "ELDI EP0, 0x%04X%016llX", hi, lo);
            } else if (e->is_double) {
                uint64_t bits;
                double dv = e->fval;
                memcpy(&bits, &dv, sizeof(bits));
                cg_emit(cg, "DLDI DP0, 0x%016llX", (unsigned long long)bits);
            } else {
                cg_emit(cg, "LET A, DWORD %lld", e->ival);
                if (e->type_size == 8) {
                    cg_emit(cg, "LET D1, DWORD %lld", (long long)((unsigned long long)e->ival >> 32));
                }
            }
            break;
        case EXPR_REGDIR:
            cg_emit(cg, "MOV A, %s", e->name);
            break;
        case EXPR_STR:
            cg_emit(cg, "LET A, DWORD %s", cg_str_label(cg, e->name));
            break;
        case EXPR_SIZEOF:
            cg_emit(cg, "LET A, DWORD %d", sizeof_expr(cg, e->r));
            break;
        case EXPR_VAR:
            if (!local_info(cg, e->name) && !param_info(cg, e->name) &&
                !global_exists(cg->prog, e->name) && func_exists(cg->prog, e->name)) {
                cg_emit(cg, "LET A, DWORD func_%s", e->name);
                break;
            }
            if (var_is_array(cg, e)) {
                var_addr(cg, e);
                break;
            }
            if (var_is_struct(cg, e)) {
                var_addr(cg, e);
                break;
            }
            if (var_ptr_depth(cg, e) > 0) {
                var_addr(cg, e);
                cg_emit(cg, "LR DWORD A, *A");
                break;
            }
            if (var_size(cg, e) == 10) {
                var_addr(cg, e);
                cg_emit(cg, "ELD EP0, *A");
            } else if (var_is_float(cg, e)) {
                var_addr(cg, e);
                cg_emit(cg, "FLD FP0, *A");
            } else if (var_is_double(cg, e)) {
                var_addr(cg, e);
                cg_emit(cg, "DLD DP0, *A");
            } else {
                emit_load_var(cg, e);
            }
            break;
        case EXPR_UNARY:
            if (strcmp(e->op, "&") == 0) {
                gen_lvalue_addr(cg, e->r);
                break;
            }
            if (strcmp(e->op, "*") == 0) {
                gen_expr(cg, e->r);
                if (expr_elem_size(cg, e->r) == 10) {
                    cg_emit(cg, "ELD EP0, *A");
                } else if (expr_elem_is_float(cg, e->r)) {
                    cg_emit(cg, "FLD FP0, *A");
                } else if (expr_elem_is_double(cg, e->r)) {
                    cg_emit(cg, "DLD DP0, *A");
                } else {
                    emit_load_from_a(cg, expr_elem_size(cg, e->r), expr_elem_unsigned(cg, e->r));
                }
                break;
            }
            gen_expr(cg, e->r);
            if (strcmp(e->op, "-")==0 && expr_size(cg, e->r) == 10) cg_emit(cg, "ENEG EP0");
            else if (strcmp(e->op, "-")==0) cg_emit(cg, "MNE DWORD A");
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
        case EXPR_INDEX:
            gen_lvalue_addr(cg, e);
            if (expr_is_array(cg, e)) {
                /* 多维数组下标后仍是数组：保留地址（退化为指针） */
                break;
            }
            if (expr_elem_size(cg, e->l) == 10) {
                cg_emit(cg, "ELD EP0, *A");
            } else if (expr_elem_is_float(cg, e->l)) {
                cg_emit(cg, "FLD FP0, *A");
            } else if (expr_elem_is_double(cg, e->l)) {
                cg_emit(cg, "DLD DP0, *A");
            } else {
                emit_load_from_a(cg, expr_elem_size(cg, e->l), expr_elem_unsigned(cg, e->l));
            }
            break;
        case EXPR_MEMBER: {
            MemberDef *m = expr_member_def(cg, e);
            gen_lvalue_addr(cg, e);
            if (m && m->is_bitfield) {
                emit_load_bitfield(cg, m);
                break;
            }
            if (m && (m->is_array || (m->is_struct && m->ptr_depth == 0))) {
                /* 数组成员退化为指针；结构体成员右值给出地址 */
                break;
            }
            if (m && m->size == 10 && m->ptr_depth == 0) {
                cg_emit(cg, "ELD EP0, *A");
            } else if (m && m->is_float) {
                cg_emit(cg, "FLD FP0, *A");
            } else if (m && m->is_double) {
                cg_emit(cg, "DLD DP0, *A");
            } else {
                int sz = m ? (m->ptr_depth > 0 ? 4 : m->size) : 4;
                int un = m ? m->is_unsigned : 0;
                emit_load_from_a(cg, sz, un);
            }
            break;
        }
        case EXPR_COND: {
            const char *lf = cg_new_label(cg, "CD");
            const char *le = cg_new_label(cg, "CD");
            gen_expr(cg, e->l);
            cg_emit(cg, "MOV C, A");
            cg_emit(cg, "ZERO T");
            cg_emit(cg, "CMP DWORD T");
            cg_emit(cg, "LET E, DWORD %s", lf);
            cg_emit(cg, "JZ");
            gen_expr(cg, e->r);
            cg_emit(cg, "LET E, DWORD %s", le);
            cg_emit(cg, "JMP");
            cg_emit(cg, "%s:", lf);
            gen_expr(cg, e->c);
            cg_emit(cg, "%s:", le);
            break;
        }
        case EXPR_COMMA:
            gen_expr(cg, e->l);
            gen_expr(cg, e->r);
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
        case EXPR_CAST:
            gen_expr(cg, e->r);
            if (e->is_double && e->type_size == 10) emit_conv_to_long_double(cg, e->r);
            else if (e->is_float) emit_conv_to_float(cg, e->r);
            else if (e->is_double) emit_conv_to_double(cg, e->r);
            else emit_conv_to_int(cg, e->r);
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
        case STMT_ASM: {
            const char *p = s->asm_text ? s->asm_text : "";
            while (*p) {
                const char *nl = strchr(p, '\n');
                size_t len = nl ? (size_t)(nl - p) : strlen(p);
                if (len > 0) {
                    char *line = (char *)malloc(len + 1);
                    memcpy(line, p, len);
                    line[len] = 0;
                    cg_emit(cg, "%s", line);
                    free(line);
                }
                if (!nl) break;
                p = nl + 1;
            }
            break;
        }
        case STMT_DECL:
            if (s->decl_is_static) {
                /* static 局部变量已在 DATA 段分配并初始化 */
                break;
            }
            if (s->decl_is_struct && s->decl_ptr_depth == 0 && s->expr) {
                int sz = s->decl_size > 0 ? s->decl_size : 4;
                Expr tmp;
                memset(&tmp, 0, sizeof(tmp));
                tmp.kind = EXPR_VAR;
                tmp.name = s->name;
                var_addr(cg, &tmp);
                cg_emit(cg, "PUSH DWORD A");
                gen_expr(cg, s->expr);
                cg_emit(cg, "MOV R, A");
                cg_emit(cg, "POP DWORD B");
                cg_emit(cg, "LET C, DWORD %d", sz);
                const char *lp = cg_new_label(cg, "ST");
                cg_emit(cg, "%s:", lp);
                cg_emit(cg, "LOD BYTE A");
                cg_emit(cg, "ST BYTE *B, A");
                cg_emit(cg, "INC B");
                cg_emit(cg, "CDI");
                cg_emit(cg, "LET E, DWORD %s", lp);
                cg_emit(cg, "JNZ");
                break;
            }
            if (s->n_init_list > 0) {
                int esz = s->decl_base_size > 0 ? s->decl_base_size : 4;
                for (int i = 0; i < s->n_init_list; i++) {
                    Expr tmp;
                    memset(&tmp, 0, sizeof(tmp));
                    tmp.kind = EXPR_VAR;
                    tmp.name = s->name;
                    var_addr(cg, &tmp);
                    if (i != 0) cg_emit(cg, "ADD DWORD A, %d", i * esz);
                    cg_emit(cg, "MOV B, A");
                    gen_expr(cg, s->init_list[i]);
                    if (s->decl_float) {
                        emit_conv_to_float(cg, s->init_list[i]);
                        cg_emit(cg, "FST *B, FP0");
                    } else if (s->decl_double) {
                        emit_conv_to_double(cg, s->init_list[i]);
                        cg_emit(cg, "DST *B, DP0");
                    } else {
                        emit_store_to_b(cg, esz);
                    }
                }
                break;
            }
            if (s->has_str_init) {
                const char *src = cg_str_label(cg, s->str_init);
                cg_emit(cg, "LET R, DWORD %s", src);
                Expr tmp;
                memset(&tmp, 0, sizeof(tmp));
                tmp.kind = EXPR_VAR;
                tmp.name = s->name;
                var_addr(cg, &tmp);
                cg_emit(cg, "MOV B, A");
                size_t len = strlen(s->str_init) + 1;
                if (s->decl_array_len > 0 && (int)len > s->decl_array_len) len = (size_t)s->decl_array_len;
                cg_emit(cg, "LET C, DWORD %d", (int)len);
                const char *lp = cg_new_label(cg, "ST");
                cg_emit(cg, "%s:", lp);
                cg_emit(cg, "LOD BYTE A");
                cg_emit(cg, "ST BYTE *B, A");
                cg_emit(cg, "INC B");
                cg_emit(cg, "CDI");
                cg_emit(cg, "LET E, DWORD %s", lp);
                cg_emit(cg, "JNZ");
                break;
            }
            if (s->expr) {
                gen_expr(cg, s->expr);
                Expr tmp;
                memset(&tmp, 0, sizeof(tmp));
                tmp.kind = EXPR_VAR;
                tmp.name = s->name;
                if (s->decl_float && s->decl_ptr_depth == 0 && !s->decl_is_array) {
                    emit_conv_to_float(cg, s->expr);
                    var_addr(cg, &tmp);
                    cg_emit(cg, "MOV B, A");
                    cg_emit(cg, "FST *B, FP0");
                } else if (s->decl_double && s->decl_ptr_depth == 0 && !s->decl_is_array) {
                    emit_conv_to_double(cg, s->expr);
                    var_addr(cg, &tmp);
                    cg_emit(cg, "MOV B, A");
                    cg_emit(cg, "DST *B, DP0");
                } else {
                    if (s->decl_bool && s->decl_ptr_depth == 0 && !s->decl_is_array) emit_bool_normalize(cg);
                    cg_emit(cg, "PUSH DWORD A");
                    var_addr(cg, &tmp);
                    cg_emit(cg, "MOV B, A");
                    cg_emit(cg, "POP DWORD A");
                    emit_store_to_b(cg, s->decl_size > 0 ? s->decl_size : 4);
                }
            }
            break;
        case STMT_RETURN: {
            if (s->expr) {
                gen_expr(cg, s->expr);
                if (cg->cur_func && cg->cur_func->is_isr) {
                    /* ISR 不处理返回值 */
                } else if (cg->cur_func && cg->cur_func->ret_is_struct && cg->cur_func->ret_ptr_depth == 0) {
                    int sz = cg->cur_func->ret_size > 0 ? cg->cur_func->ret_size : 4;
                    cg_emit(cg, "MOV R, A");
                    cg_emit(cg, "LET B, DWORD struct_ret");
                    cg_emit(cg, "LET C, DWORD %d", sz);
                    const char *lp = cg_new_label(cg, "ST");
                    cg_emit(cg, "%s:", lp);
                    cg_emit(cg, "LOD BYTE A");
                    cg_emit(cg, "ST BYTE *B, A");
                    cg_emit(cg, "INC B");
                    cg_emit(cg, "CDI");
                    cg_emit(cg, "LET E, DWORD %s", lp);
                    cg_emit(cg, "JNZ");
                } else if (expr_size(cg, s->expr) != 8 && !expr_is_float(cg, s->expr) && !expr_is_double(cg, s->expr)) {
                    cg_emit(cg, "MOV D1, A");
                }
            }
            if (cg->cur_func && cg->cur_func->is_isr) {
                cg_emit(cg, "MOV S, F");
                cg_emit(cg, "POP DWORD F");
                cg_emit(cg, "IRET");
            } else {
                cg_emit(cg, "RER");
                cg_emit(cg, "JMP");
            }
            break;
        }
        case STMT_SWITCH: {
            const char *end = cg_new_label(cg, "SW");
            gen_expr(cg, s->cond);
            cg_emit(cg, "PUSH DWORD A");
            int nc = s->body ? s->body->nitems : 0;
            char **labels = (char **)calloc((size_t)(nc > 0 ? nc : 1), sizeof(char *));
            const char *def = NULL;
            for (int i = 0; i < nc; i++) {
                Stmt *it = s->body->items[i];
                if (it->kind == STMT_CASE) {
                    labels[i] = cg_new_label(cg, "CS");
                    if (!it->expr) def = labels[i];
                }
            }
            for (int i = 0; i < nc; i++) {
                Stmt *it = s->body->items[i];
                if (it->kind != STMT_CASE || !it->expr) continue;
                gen_expr(cg, it->expr);
                cg_emit(cg, "MOV B, A");
                cg_emit(cg, "POP DWORD C");
                cg_emit(cg, "PUSH DWORD C");
                cg_emit(cg, "CMP DWORD B");
                cg_emit(cg, "LET E, DWORD %s", labels[i]);
                cg_emit(cg, "JZ");
            }
            if (def) {
                cg_emit(cg, "LET E, DWORD %s", def);
            } else {
                cg_emit(cg, "LET E, DWORD %s", end);
            }
            cg_emit(cg, "JMP");
            for (int i = 0; i < nc; i++) {
                Stmt *it = s->body->items[i];
                if (it->kind != STMT_CASE) continue;
                cg_emit(cg, "%s:", labels[i]);
                cg_push_loop(cg, end, end);
                gen_stmt(cg, it->body);
                cg_pop_loop(cg);
                /* 不在这里跳转到 end：支持 C 的 case 穿透（fallthrough） */
            }
            cg_emit(cg, "%s:", end);
            free(labels);
            break;
        }
        case STMT_CASE:
            break;
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
        case STMT_DO: {
            const char *ls = cg_new_label(cg, "DL");
            const char *lc = cg_new_label(cg, "DC");
            const char *le = cg_new_label(cg, "DE");
            cg_emit(cg, "%s:", ls);
            cg_push_loop(cg, le, lc);
            gen_stmt(cg, s->body);
            cg_pop_loop(cg);
            cg_emit(cg, "%s:", lc);
            gen_expr(cg, s->cond);
            cg_emit(cg, "MOV C, A");
            cg_emit(cg, "ZERO T");
            cg_emit(cg, "CMP DWORD T");
            cg_emit(cg, "LET E, DWORD %s", ls);
            cg_emit(cg, "JNZ");
            cg_emit(cg, "%s:", le);
            break;
        }
        case STMT_LABEL:
            cg_emit(cg, "%s:", s->name);
            break;
        case STMT_GOTO:
            cg_emit(cg, "LET E, DWORD %s", s->name);
            cg_emit(cg, "JMP");
            break;
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
                      f->param_unsigned ? f->param_unsigned[i] : 0,
                      f->param_float ? f->param_float[i] : 0,
                      f->param_double ? f->param_double[i] : 0,
                      f->param_const ? f->param_const[i] : 0,
                      f->param_bool ? f->param_bool[i] : 0,
                      f->param_ptr_depth ? f->param_ptr_depth[i] : 0,
                      0, 0, NULL, 0,
                      f->param_elem_size ? f->param_elem_size[i] : 0,
                      f->param_base_size ? f->param_base_size[i] : 0,
                      f->param_is_struct ? f->param_is_struct[i] : 0,
                      f->param_struct_name ? f->param_struct_name[i] : NULL,
                      f->param_is_func_ptr ? f->param_is_func_ptr[i] : 0,
                      f->param_func_ret_size ? f->param_func_ret_size[i] : 0,
                      f->param_func_ret_float ? f->param_func_ret_float[i] : 0,
                      f->param_func_ret_double ? f->param_func_ret_double[i] : 0,
                      f->param_func_ret_void ? f->param_func_ret_void[i] : 0,
                      f->param_func_ret_is_struct ? f->param_func_ret_is_struct[i] : 0,
                      f->param_func_ret_struct_name ? f->param_func_ret_struct_name[i] : NULL);
    }
    int frame = 4;
    collect_locals(cg, f->body, f->nbody, &frame);
    cg->frame_size = frame;
    cg->cur_func = f;
    cg->cur_func_named_bytes = 0;
    for (int i = 0; i < f->nparams; i++)
        cg->cur_func_named_bytes += (f->param_sizes && f->param_sizes[i] > 0) ? f->param_sizes[i] : 4;
    cg->cur_func_is_vararg = f->is_vararg;
    cg_emit(cg, "func_%s:", f->name);
    if (f->is_isr) cg_emit(cg, "PUSH DWORD F");
    cg_emit(cg, "SFA DWORD %d", frame);
    cg->nloops = 0;
    for (int i = 0; i < f->nbody; i++) gen_stmt(cg, f->body[i]);
    if (f->is_isr) {
        cg_emit(cg, "MOV S, F");
        cg_emit(cg, "POP DWORD F");
        cg_emit(cg, "IRET");
    } else if (f->ret_void) {
        cg_emit(cg, "RER");
        cg_emit(cg, "JMP");
    } else {
        if (f->ret_bool) emit_bool_normalize(cg);
        if (f->ret_size != 8 && !f->ret_float && !f->ret_double)
            cg_emit(cg, "MOV D1, A");
        cg_emit(cg, "RER");
        cg_emit(cg, "JMP");
    }
}

static void emit_static_local_data(CodeGen *cg, Stmt *s) {
    char label[64];
    snprintf(label, sizeof(label), "var_static_%d", cg->label++);
    s->static_label = xstrdup(label);
    fprintf(cg->out, "%s:\n", label);
    int sz = s->decl_size > 0 ? s->decl_size : 4;
    if (s->n_init_list > 0) {
        int unit = s->decl_base_size > 0 ? s->decl_base_size : 4;
        int count = s->decl_array_len > 0 ? s->decl_array_len : s->n_init_list;
        for (int i = 0; i < count; i++) {
            long long val = i < s->n_init_list && s->init_list[i]->kind == EXPR_NUM ? s->init_list[i]->ival : 0;
            if (unit == 8) {
                fprintf(cg->out, "\tDD %d, %lld\n", cg->data_off, val);
                fprintf(cg->out, "\tDD %d, %lld\n", cg->data_off + 4, (long long)((unsigned long long)val >> 32));
            } else if (unit == 1) {
                fprintf(cg->out, "\tDB %d, 0x%02X\n", cg->data_off, (int)(val & 0xff));
            } else if (unit == 2) {
                fprintf(cg->out, "\tDW %d, %d\n", cg->data_off, (int)(val & 0xffff));
            } else {
                fprintf(cg->out, "\tDD %d, %lld\n", cg->data_off, val);
            }
            cg->data_off += unit;
        }
        return;
    }
    if (s->has_str_init && s->decl_base_size == 1) {
        size_t len = strlen(s->str_init) + 1;
        int max = s->decl_array_len > 0 ? s->decl_array_len : (int)len;
        if ((int)len > max) len = (size_t)max;
        for (size_t i = 0; i < len; i++) {
            unsigned char v = i < strlen(s->str_init) ? (unsigned char)s->str_init[i] : 0;
            fprintf(cg->out, "\tDB %d, 0x%02X\n", cg->data_off++, v);
        }
        if (max > (int)len) {
            fprintf(cg->out, "\tRESB %d\n", max - (int)len);
            cg->data_off += max - (int)len;
        }
        return;
    }
    if (s->decl_is_array) {
        fprintf(cg->out, "\tRESB %d\n", sz);
        cg->data_off += sz;
        return;
    }
    if (sz == 10) {
        if (s->expr && s->expr->kind == EXPR_NUM && s->expr->is_double && s->expr->type_size == 10) {
            unsigned long long lo;
            unsigned int hi;
            double_to_ext80(s->expr->fval, &lo, &hi);
            for (int i = 0; i < 8; i++)
                fprintf(cg->out, "\tDB %d, 0x%02X\n", cg->data_off + i, (int)((lo >> (8 * i)) & 0xff));
            fprintf(cg->out, "\tDB %d, 0x%02X\n", cg->data_off + 8, hi & 0xff);
            fprintf(cg->out, "\tDB %d, 0x%02X\n", cg->data_off + 9, (hi >> 8) & 0xff);
            cg->data_off += sz;
        } else {
            fprintf(cg->out, "\tRESB %d\n", sz);
            cg->data_off += sz;
        }
        return;
    }
    long long val = (s->expr && s->expr->kind == EXPR_NUM) ? s->expr->ival : 0;
    if (sz == 8) {
        fprintf(cg->out, "\tDD %d, %lld\n", cg->data_off, val);
        fprintf(cg->out, "\tDD %d, %lld\n", cg->data_off + 4, (long long)((unsigned long long)val >> 32));
    } else if (sz == 1) {
        fprintf(cg->out, "\tDB %d, 0x%02X\n", cg->data_off, (int)(val & 0xff));
    } else if (sz == 2) {
        fprintf(cg->out, "\tDW %d, %d\n", cg->data_off, (int)(val & 0xffff));
    } else {
        fprintf(cg->out, "\tDD %d, %lld\n", cg->data_off, val);
    }
    cg->data_off += sz;
}

static void collect_static_locals(CodeGen *cg, Stmt **stmts, int n) {
    for (int i = 0; i < n; i++) {
        Stmt *s = stmts[i];
        if (!s) continue;
        if (s->kind == STMT_DECL && s->decl_is_static) {
            emit_static_local_data(cg, s);
        } else if (s->kind == STMT_BLOCK) {
            collect_static_locals(cg, s->items, s->nitems);
        } else if (s->kind == STMT_IF) {
            Stmt *tmp[1];
            if (s->then) { tmp[0] = s->then; collect_static_locals(cg, tmp, 1); }
            if (s->els) { tmp[0] = s->els; collect_static_locals(cg, tmp, 1); }
        } else if (s->kind == STMT_WHILE || s->kind == STMT_DO) {
            Stmt *tmp[1] = {s->body};
            collect_static_locals(cg, tmp, 1);
        } else if (s->kind == STMT_FOR) {
            Stmt *tmp[1] = {s->body};
            collect_static_locals(cg, tmp, 1);
        }
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
    cg.data_off = 0;
    collect_program_strings(&cg, p);
    fprintf(out, "\tSECTION DATA\n\tORG 0\n");
    for (int i = 0; i < p->nglobals; i++) {
        Global *g = &p->globals[i];
        if (g->is_extern && !g->has_init && !g->has_str_init && g->n_init_list == 0)
            continue;
        fprintf(out, "var_%s:\n", g->name);
        if (g->has_str_init) {
            if (g->is_array && g->base_size == 1) {
                size_t len = strlen(g->str_init) + 1;
                int max = g->array_len > 0 ? g->array_len : (int)len;
                if ((int)len > max) len = (size_t)max;
                for (size_t e = 0; e < len; e++) {
                    unsigned char v = (e < strlen(g->str_init)) ? (unsigned char)g->str_init[e] : 0;
                    fprintf(out, "\tDB %d, 0x%02X\n", cg.data_off++, v);
                }
                if (max > (int)len) {
                    fprintf(out, "\tRESB %d\n", max - (int)len);
                    cg.data_off += max - (int)len;
                }
            } else {
                const char *label = cg_str_label(&cg, g->str_init);
                fprintf(out, "\tDD %d, %s\n", cg.data_off, label);
                cg.data_off += 4;
            }
            continue;
        }
        if (g->is_array && g->n_init_list > 0) {
            int unit = g->base_size > 0 ? g->base_size : 4;
            int count = g->array_len > 0 ? g->array_len : g->n_init_list;
            if (count <= 0) count = 1;
            for (int e = 0; e < count; e++) {
                long long val = e < g->n_init_list ? g->init_list[e] : 0;
                if (unit == 10) {
                    fprintf(out, "\tRESB %d\n", unit);
                } else if (unit == 8) {
                    fprintf(out, "\tDD %d, %lld\n", cg.data_off, val);
                    fprintf(out, "\tDD %d, %lld\n", cg.data_off + 4, (long long)((unsigned long long)val >> 32));
                } else if (unit == 1) {
                    fprintf(out, "\tDB %d, 0x%02X\n", cg.data_off, (int)(val & 0xff));
                } else if (unit == 2) {
                    fprintf(out, "\tDW %d, %d\n", cg.data_off, (int)(val & 0xffff));
                } else {
                    fprintf(out, "\tDD %d, %lld\n", cg.data_off, val);
                }
                cg.data_off += unit;
            }
            continue;
        }
        int gsz = g->type_size > 0 ? g->type_size : 4;
        if (g->is_struct && g->ptr_depth == 0 && !g->is_array) {
            fprintf(out, "\tRESB %d\n", gsz);
            cg.data_off += gsz;
            continue;
        }
        int unit = g->base_size > 0 ? g->base_size : (g->is_array ? 4 : gsz);
        int count = g->is_array ? (g->array_len > 0 ? g->array_len : 1) : (unit > 0 ? gsz / unit : 1);
        if (count <= 0) count = 1;
        for (int e = 0; e < count; e++) {
            int val = (g->has_init && !g->is_array && e == 0) ? g->init : 0;
            if (unit == 10) {
                if (g->has_init && !g->is_array) {
                    unsigned long long lo;
                    unsigned int hi;
                    double_to_ext80(g->init_f, &lo, &hi);
                    for (int i = 0; i < 8; i++)
                        fprintf(out, "\tDB %d, 0x%02X\n", cg.data_off + i, (int)((lo >> (8 * i)) & 0xff));
                    fprintf(out, "\tDB %d, 0x%02X\n", cg.data_off + 8, hi & 0xff);
                    fprintf(out, "\tDB %d, 0x%02X\n", cg.data_off + 9, (hi >> 8) & 0xff);
                } else {
                    fprintf(out, "\tRESB %d\n", unit);
                }
            } else if (unit == 8) {
                fprintf(out, "\tDD %d, %d\n", cg.data_off, val);
                fprintf(out, "\tDD %d, 0\n", cg.data_off + 4);
            } else if (unit == 1) {
                fprintf(out, "\tDB %d, 0x%02X\n", cg.data_off, val & 0xff);
            } else if (unit == 2) {
                fprintf(out, "\tDW %d, %d\n", cg.data_off, val & 0xffff);
            } else {
                fprintf(out, "\tDD %d, %d\n", cg.data_off, val);
            }
            cg.data_off += unit;
        }
    }
    for (int i = 0; i < cg.nstrings; i++) {
        emit_str_data(&cg, cg.strings[i].label, cg.strings[i].str);
    }
    for (int i = 0; i < p->nfuncs; i++) {
        if (!p->funcs[i].is_decl)
            collect_static_locals(&cg, p->funcs[i].body, p->funcs[i].nbody);
    }
    int max_ret_struct = 0;
    for (int i = 0; i < p->nfuncs; i++) {
        if (p->funcs[i].ret_is_struct && p->funcs[i].ret_ptr_depth == 0) {
            int sz = p->funcs[i].ret_size;
            if (sz > max_ret_struct) max_ret_struct = sz;
        }
    }
    if (max_ret_struct > 0) {
        fprintf(out, "struct_ret:\n\tRESB %d\n", max_ret_struct);
        cg.data_off += max_ret_struct;
    }
    fprintf(out, "\n\tSECTION TEXT\n\tORG 0\n");
    for (int i = 0; i < p->nfuncs; i++) {
        if (!p->funcs[i].is_decl) gen_func(&cg, &p->funcs[i]);
    }
    for (int i = 0; i < cg.nstrings; i++) {
        free(cg.strings[i].str);
        free(cg.strings[i].label);
    }
    free(cg.strings);
    fclose(out);
    if (err) *err = NULL;
    return 1;
}
