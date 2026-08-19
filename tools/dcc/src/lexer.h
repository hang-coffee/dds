// lexer.h - dcc 词法分析器
#ifndef DCC_LEXER_H
#define DCC_LEXER_H

#include "token.h"
#include <string>
#include <vector>

namespace dcc {

class Lexer {
public:
	explicit Lexer(const std::string& src);
	std::vector<Token> tokenize();
	const std::vector<std::string>& errors() const { return errs_; }

private:
	const std::string& src_;
	size_t pos_;
	int line_;
	std::vector<std::string> errs_;

	char peek(size_t off = 0) const;
	char advance();
	void skip_whitespace_and_comments();
	Token make(TokenKind k, const std::string& txt);
	bool is_ident_start(char c) const;
	bool is_ident_part(char c) const;
};

} // namespace dcc

#endif
