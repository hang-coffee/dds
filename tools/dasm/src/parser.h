#ifndef DASM_PARSER_H
#define DASM_PARSER_H

#include "token.h"
#include "symbol.h"
#include "encoder.h"
#include "generator.h"
#include <vector>
#include <string>

namespace dasm {

struct parser_context {
    const std::vector<token>* tokens;
    size_t current_token;
    symbol_table* symtab;
    generator_context* gen;
    int current_line;
    std::vector<std::string> errors;
    bool is_pass1;
};

void parser_init(parser_context& ctx, const std::vector<token>& tokens,
                 symbol_table& symtab, generator_context& gen);

bool parser_assemble(parser_context& ctx, bool verbose);

bool parser_pass1(parser_context& ctx);
bool parser_pass2(parser_context& ctx);

const token* parser_peek(parser_context& ctx);
bool parser_consume(parser_context& ctx);
bool parser_is_eof(parser_context& ctx);

} // namespace dasm

#endif
