#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_part(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static int is_keyword(const char *s) {
    static const char *kw[] = {
        "int", "char", "void", "return", "if", "else",
        "while", "for", "break", "continue", NULL
    };
    for (int i = 0; kw[i]; i++)
        if (strcmp(s, kw[i]) == 0) return 1;
    return 0;
}

static void ta_push(TokenArray *ta, Token t) {
    if (ta->count >= ta->cap) {
        ta->cap = ta->cap ? ta->cap * 2 : 64;
        ta->toks = (Token *)realloc(ta->toks, (size_t)ta->cap * sizeof(Token));
    }
    ta->toks[ta->count++] = t;
}

TokenArray tokenize(const char *src) {
    TokenArray ta;
    memset(&ta, 0, sizeof(ta));
    int line = 1;
    size_t i = 0;
    while (src[i]) {
        char c = src[i];
        if (c == '\n') { line++; i++; continue; }
        if (isspace((unsigned char)c)) { i++; continue; }
        if (c == '/' && src[i+1] == '/') {
            while (src[i] && src[i] != '\n') i++;
            continue;
        }
        if (c == '/' && src[i+1] == '*') {
            i += 2;
            while (src[i] && !(src[i] == '*' && src[i+1] == '/')) {
                if (src[i] == '\n') line++;
                i++;
            }
            if (src[i]) i += 2;
            continue;
        }
        if (isdigit((unsigned char)c)) {
            Token t;
            memset(&t, 0, sizeof(t));
            t.kind = T_NUM;
            t.line = line;
            int base = 10;
            const char *start = src + i;
            if (c == '0' && (src[i+1] == 'x' || src[i+1] == 'X')) {
                base = 16;
                i += 2;
                t.ival = 0;
                while (isxdigit((unsigned char)src[i])) {
                    int d = isdigit((unsigned char)src[i]) ? src[i]-'0' : tolower((unsigned char)src[i])-'a'+10;
                    t.ival = t.ival * base + d;
                    i++;
                }
            } else {
                t.ival = 0;
                while (isdigit((unsigned char)src[i])) {
                    t.ival = t.ival * 10 + (src[i]-'0');
                    i++;
                }
            }
            size_t len = (size_t)(src + i - start);
            t.text = (char *)malloc(len + 1);
            memcpy(t.text, start, len);
            t.text[len] = 0;
            ta_push(&ta, t);
            continue;
        }
        if (is_ident_start(c)) {
            const char *start = src + i;
            while (is_ident_part(src[i])) i++;
            size_t len = (size_t)(src + i - start);
            char *word = (char *)malloc(len + 1);
            memcpy(word, start, len);
            word[len] = 0;
            Token t;
            memset(&t, 0, sizeof(t));
            t.text = word;
            t.line = line;
            t.kind = is_keyword(word) ? T_KW : T_ID;
            ta_push(&ta, t);
            continue;
        }
        /* operators */
        static const char *ops2[] = {
            ">>=", "<<=", ">>", "<<", "<=", ">=", "==", "!=",
            "&&", "||", "++", "--", "+=", "-=", "*=", "/=",
            "%=", "&=", "|=", "^=", NULL
        };
        int matched = 0;
        for (int k = 0; ops2[k]; k++) {
            size_t l = strlen(ops2[k]);
            if (strncmp(src + i, ops2[k], l) == 0) {
                Token t;
                memset(&t, 0, sizeof(t));
                t.kind = T_OP;
                t.text = xstrdup(ops2[k]);
                t.line = line;
                ta_push(&ta, t);
                i += l;
                matched = 1;
                break;
            }
        }
        if (matched) continue;
        if (strchr("+-*/%&|^~!<>=(),;{}[]?:", c)) {
            Token t;
            memset(&t, 0, sizeof(t));
            t.kind = T_OP;
            t.text = (char *)malloc(2);
            t.text[0] = c;
            t.text[1] = 0;
            t.line = line;
            ta_push(&ta, t);
            i++;
            continue;
        }
        fprintf(stderr, "dcc-c99: line %d: 无法识别的字符 '%c'\n", line, c);
        i++;
    }
    Token eof;
    memset(&eof, 0, sizeof(eof));
    eof.kind = T_EOF;
    eof.text = xstrdup("");
    eof.line = line;
    ta_push(&ta, eof);
    return ta;
}

void token_array_free(TokenArray *ta) {
    for (int i = 0; i < ta->count; i++) free(ta->toks[i].text);
    free(ta->toks);
    memset(ta, 0, sizeof(*ta));
}
