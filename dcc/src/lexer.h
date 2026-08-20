#ifndef DCC_C99_LEXER_H
#define DCC_C99_LEXER_H

typedef enum {
    T_NUM,
    T_FLOAT,
    T_STR,
    T_ID,
    T_KW,
    T_OP,
    T_EOF
} TokenKind;

typedef struct {
    TokenKind kind;
    char *text;
    long long ival;
    double fval;
    int line;
    int is_long;
    int is_unsigned;
    int is_double;
} Token;

typedef struct {
    Token *toks;
    int count;
    int cap;
} TokenArray;

TokenArray tokenize(const char *src);
void token_array_free(TokenArray *ta);

#endif
