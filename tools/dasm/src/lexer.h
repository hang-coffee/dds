#ifndef DASM_LEXER_H
#define DASM_LEXER_H

#include "token.h"
#include <string>
#include <vector>

namespace dasm {

struct lexer_context {
    // 每行携带原始源文件行号（预处理后行号会偏移，这里保留真实行号）
    std::vector<std::pair<int, std::string>> lines;
    size_t current_line;
    size_t current_pos;
    int line_no;
    std::string error;

    // NZ 拼接形式（如 ADDNZ）会一次性产出两个 token，第二个暂存于此
    token pending_tok;
    bool has_pending;
};

void lexer_init(lexer_context& ctx, const std::vector<std::pair<int, std::string>>& lines);
bool lexer_next_token(lexer_context& ctx, token& out_tok);
bool lexer_peek_token(lexer_context& ctx, token& out_tok);

token_type lexer_instruction_to_type(const std::string& name);
token_type lexer_register_to_type(const std::string& name);
token_type lexer_sysreg_to_type(const std::string& name);

} // namespace dasm

#endif
