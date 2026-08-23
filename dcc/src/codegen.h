#ifndef DCC_C99_CODEGEN_H
#define DCC_C99_CODEGEN_H

#include "ast.h"

int generate_code(Program *p, const char *outpath, int emit_extern, char **err);

#endif
