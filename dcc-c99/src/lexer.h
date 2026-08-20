#ifndef DCC_C99_LEXER_H
#define DCC_C99_LEXER_H

typedef enum {
    T_NUM,
    T_ID,
    T_KW,
    T_OP,
    T_EOF
} TokenKind;

typedef struct {
    TokenKind kind;
    char *text;
    int ival;
    int line;
} Token;

typedef struct {
    Token *toks;
    int count;
    int cap;
} TokenArray;

TokenArray tokenize(const char *src);
void token_array_free(TokenArray *ta);

#endif
