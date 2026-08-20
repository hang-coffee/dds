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
    EXPR_CAST
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
    char *op;              /* EXPR_BIN / EXPR_UNARY / EXPR_ASSIGN / EXPR_INCDEC */
    Expr *l, *r;           /* children */
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
    STMT_CONTINUE
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
    int decl_size;         /* STMT_DECL */
    int decl_unsigned;     /* STMT_DECL */
    int decl_float;         /* STMT_DECL */
    int decl_double;        /* STMT_DECL */
    int decl_const;         /* STMT_DECL */
    int decl_bool;          /* STMT_DECL */
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
    int ret_bool;
} Function;

typedef struct {
    char *name;
    int has_init;
    int init;
    int type_size;
    int is_unsigned;
    int is_float;
    int is_double;
    int is_const;
    int is_bool;
} Global;

typedef struct {
    Global *globals;
    int nglobals;
    Function *funcs;
    int nfuncs;
} Program;

void program_free(Program *p);
void stmt_free(Stmt *s);
void expr_free(Expr *e);

#endif
