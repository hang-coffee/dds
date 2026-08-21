#ifndef DCC_C99_AST_H
#define DCC_C99_AST_H

typedef struct Expr Expr;
typedef struct Stmt Stmt;

typedef enum {
    EXPR_NUM,
    EXPR_VAR,
    EXPR_BIN,
    EXPR_UNARY,
    EXPR_ASSIGN,
    EXPR_CALL,
    EXPR_INCDEC,
    EXPR_CAST,
    EXPR_INDEX,
    EXPR_REGDIR,
    EXPR_STR,
    EXPR_MEMBER,
    EXPR_SIZEOF,
    EXPR_COND,
    EXPR_COMMA
} ExprKind;

struct Expr {
    ExprKind kind;
    long long ival;        /* EXPR_NUM */
    double fval;           /* EXPR_NUM 浮点字面量 */
    int type_size;         /* EXPR_NUM / 表达式类型尺寸 */
    int is_unsigned;       /* EXPR_NUM / 表达式是否无符号 */
    int is_float;          /* 表达式是否为 float */
    int is_double;         /* 表达式是否为 double */
    char *name;            /* EXPR_VAR / EXPR_CALL */
    char *member;          /* EXPR_MEMBER */
    int arrow;             /* EXPR_MEMBER: 1=->, 0=. */
    char *op;              /* EXPR_BIN / EXPR_UNARY / EXPR_ASSIGN / EXPR_INCDEC */
    Expr *l, *r, *c;       /* children */
    Expr **args;           /* EXPR_CALL */
    int nargs;
    int postfix;           /* EXPR_INCDEC */
    int line;
};

typedef enum {
    STMT_EMPTY,
    STMT_EXPR,
    STMT_RETURN,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_BLOCK,
    STMT_DECL,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_ASM,
    STMT_DO,
    STMT_LABEL,
    STMT_GOTO,
    STMT_SWITCH,
    STMT_CASE
} StmtKind;

struct Stmt {
    StmtKind kind;
    Expr *expr;            /* STMT_EXPR / STMT_RETURN / STMT_DECL init */
    Expr *cond;            /* STMT_IF / STMT_WHILE / STMT_FOR */
    Stmt *then, *els;      /* STMT_IF */
    Stmt *body;            /* STMT_WHILE / STMT_FOR */
    Expr *init, *inc;      /* STMT_FOR */
    Stmt **items;          /* STMT_BLOCK */
    int nitems;
    char *name;            /* STMT_DECL */
    char *asm_text;        /* STMT_ASM */
    char *str_init;        /* STMT_DECL：char 数组字符串初始化 */
    int has_str_init;      /* STMT_DECL */
    int decl_is_static;    /* STMT_DECL */
    char *static_label;    /* STMT_DECL：static 局部变量在 DATA 段中的标号 */
    Expr **init_list;      /* STMT_DECL：数组初始化列表 */
    int n_init_list;
    int decl_is_struct;    /* STMT_DECL */
    char *decl_struct_name;/* STMT_DECL */
    int decl_is_func_ptr;
    int decl_func_ret_size;
    int decl_func_ret_float;
    int decl_func_ret_double;
    int decl_func_ret_void;
    int decl_func_ret_is_struct;
    char *decl_func_ret_struct_name;
    int decl_size;         /* STMT_DECL：标量/指针为存储尺寸，数组为总字节数 */
    int decl_unsigned;     /* STMT_DECL：元素（非指针）是否无符号 */
    int decl_float;         /* STMT_DECL：元素是否为 float */
    int decl_double;        /* STMT_DECL：元素是否为 double */
    int decl_const;         /* STMT_DECL */
    int decl_bool;          /* STMT_DECL */
    int decl_ptr_depth;     /* STMT_DECL：指针深度 */
    int decl_is_array;      /* STMT_DECL */
    int decl_ndims;         /* STMT_DECL：数组维度个数 */
    int *decl_dims;         /* STMT_DECL：各维长度 */
    int decl_array_len;     /* STMT_DECL：数组元素总数 */
    int decl_elem_size;     /* STMT_DECL：第一维元素/指针指向的单个元素字节数 */
    int decl_base_size;     /* STMT_DECL：基础标量类型字节数（1/2/4/8） */
    Stmt *next;            /* 未使用，保留 */
};

typedef struct {
    char *name;
    char **params;
    int *param_sizes;
    int *param_unsigned;
    int nparams;
    Stmt **body;
    int nbody;
    int ret_void;
    int ret_size;
    int ret_unsigned;
    int ret_float;
    int ret_double;
    int *param_float;
    int *param_double;
    int *param_const;
    int *param_bool;
    int *param_ptr_depth;
    int *param_elem_size;
    int *param_base_size;
    int *param_is_struct;
    char **param_struct_name;
    int ret_bool;
    int ret_ptr_depth;
    int ret_elem_size;
    int ret_base_size;
    int ret_is_struct;
    char *ret_struct_name;
    int is_decl;
    int *param_is_func_ptr;
    int *param_func_ret_size;
    int *param_func_ret_float;
    int *param_func_ret_double;
    int *param_func_ret_void;
    int *param_func_ret_is_struct;
    char **param_func_ret_struct_name;
    int is_inline;
    int is_static;
    int is_isr;
    int is_vararg;
} Function;

typedef struct {
    char *name;
    int has_init;
    int init;
    double init_f;      /* 全局浮点初始化值（float/double/long double） */
    char *str_init;
    int has_str_init;
    long long *init_list;
    int n_init_list;
    int is_extern;
    int is_static;
    int type_size;         /* 标量/指针存储尺寸；数组为总字节数 */
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
} Global;

typedef struct MemberDef {
    char *name;
    int offset;
    int size;
    int elem_size;
    int base_size;
    int is_unsigned;
    int is_float;
    int is_double;
    int is_bool;
    int is_struct;
    int is_array;
    int array_len;
    int ptr_depth;
    char *struct_name;
    int is_bitfield;
    int bit_offset;
    int bit_width;
    int bit_unit_size;
} MemberDef;

typedef struct StructDef {
    char *name;
    MemberDef *members;
    int nmembers;
    int size;
    int is_union;
} StructDef;

typedef struct {
    Global *globals;
    int nglobals;
    Function *funcs;
    int nfuncs;
    StructDef *structs;
    int nstructs;
} Program;

void program_free(Program *p);
void stmt_free(Stmt *s);
void expr_free(Expr *e);

#endif
