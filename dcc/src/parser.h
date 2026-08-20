#ifndef DCC_C99_PARSER_H
#define DCC_C99_PARSER_H

#include "ast.h"
#include "lexer.h"

Program parse_program(TokenArray *ta, char **err);
void parse_error_free(char *err);

#endif
