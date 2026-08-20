// lexer.cpp - dcc 词法分析器实现
#include "lexer.h"
#include <cctype>
#include <cstdlib>

namespace dcc {

Lexer::Lexer(const std::string& src) : src_(src), pos_(0), line_(1) {}

char Lexer::peek(size_t off) const {
	size_t p = pos_ + off;
	return (p < src_.size()) ? src_[p] : '\0';
}

char Lexer::advance() {
	if (pos_ < src_.size()) {
		if (src_[pos_] == '\n') line_++;
		return src_[pos_++];
	}
	return '\0';
}

bool Lexer::is_ident_start(char c) const {
	return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool Lexer::is_ident_part(char c) const {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

Token Lexer::make(TokenKind k, const std::string& txt) {
	Token t;
	t.kind = k;
	t.text = txt;
	t.ival = 0;
	t.line = line_;
	t.is_long = false;
	return t;
}

void Lexer::skip_whitespace_and_comments() {
	for (;;) {
		while (std::isspace(static_cast<unsigned char>(peek()))) advance();
		// 行注释 //
		if (peek() == '/' && peek(1) == '/') {
			while (peek() != '\n' && peek() != '\0') advance();
			continue;
		}
		// 块注释 /* */
		if (peek() == '/' && peek(1) == '*') {
			advance(); advance();
			while (!(peek() == '*' && peek(1) == '/')) {
				if (peek() == '\0') { errs_.push_back("未闭合的块注释"); return; }
				advance();
			}
			advance(); advance();
			continue;
		}
		break;
	}
}

static TokenKind keyword_kind(const std::string& s) {
	if (s == "int") return TOK_INT;
	if (s == "float") return TOK_FLOAT;
	if (s == "double") return TOK_DOUBLE;
	if (s == "short") return TOK_SHORT;
	if (s == "long") return TOK_LONG;
	if (s == "char") return TOK_CHAR;
	if (s == "_Bool") return TOK_BOOL;
	if (s == "const") return TOK_CONST;
	if (s == "sizeof") return TOK_SIZEOF;
	if (s == "__interrupt__") return TOK_INTERRUPT;
	if (s == "struct") return TOK_STRUCT;
	if (s == "union") return TOK_UNION;
	if (s == "enum") return TOK_ENUM;
	if (s == "typedef") return TOK_TYPEDEF;
	if (s == "void") return TOK_VOID;
	if (s == "signed") return TOK_SIGNED;
	if (s == "unsigned") return TOK_UNSIGNED;
	if (s == "extern") return TOK_EXTERN;
if (s == "static") return TOK_STATIC;
if (s == "inline") return TOK_INLINE;
	if (s == "__asm__" || s == "asm") return TOK_ASM;
	if (s == "return") return TOK_RETURN;
	if (s == "if") return TOK_IF;
	if (s == "else") return TOK_ELSE;
	if (s == "while") return TOK_WHILE;
	if (s == "for") return TOK_FOR;
	if (s == "break") return TOK_BREAK;
	if (s == "continue") return TOK_CONTINUE;
	return TOK_IDENT;
}

std::vector<Token> Lexer::tokenize() {
	std::vector<Token> toks;
	for (;;) {
		skip_whitespace_and_comments();
		char c = peek();
		if (c == '\0') { toks.push_back(make(TOK_EOF, "")); break; }

		// 标识符 / 关键字
		if (is_ident_start(c)) {
			size_t start = pos_;
			while (is_ident_part(peek())) advance();
			std::string word = src_.substr(start, pos_ - start);
			Token t = make(keyword_kind(word), word);
			toks.push_back(t);
			continue;
		}
		// 数字
		if (std::isdigit(static_cast<unsigned char>(c))) {
			size_t start = pos_;
			std::string num;
			if (c == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
				advance(); advance();
				while (std::isxdigit(static_cast<unsigned char>(peek()))) {
					num += advance();
				}
				Token t = make(TOK_NUMBER, src_.substr(start, pos_ - start));
				t.ival = std::strtoll(("0x" + num).c_str(), nullptr, 16);
				toks.push_back(t);
				continue;
			}
			// 十进制 / 八进制 / 浮点
			while (std::isdigit(static_cast<unsigned char>(peek()))) num += advance();
			bool is_float = false;
			if (peek() == '.') {
				is_float = true;
				num += advance();
				while (std::isdigit(static_cast<unsigned char>(peek()))) num += advance();
			}
			if (peek() == 'e' || peek() == 'E') {
				is_float = true;
				num += advance();
				if (peek() == '+' || peek() == '-') num += advance();
				while (std::isdigit(static_cast<unsigned char>(peek()))) num += advance();
			}
			bool long_suffix = false;
			if (peek() == 'f' || peek() == 'F') {
				is_float = true;
				advance();
			} else if (peek() == 'l' || peek() == 'L') {
				if (is_float) {
					// 1.0L / 1e10L：long double
					is_float = true;
					advance();
				} else {
					// 1L / 0x10L：long 整数
					long_suffix = true;
					advance();
				}
			}
			if (is_float) {
				Token t = make(TOK_FLOATLIT, src_.substr(start, pos_ - start));
				t.fval = std::strtod(num.c_str(), nullptr);
				toks.push_back(t);
			} else if (num.size() > 1 && num[0] == '0' && !long_suffix) {	// 八进制
				Token t = make(TOK_NUMBER, src_.substr(start, pos_ - start));
				t.ival = std::strtoll(num.c_str(), nullptr, 8);
				t.is_long = long_suffix;
				toks.push_back(t);
			} else {
				Token t = make(TOK_NUMBER, src_.substr(start, pos_ - start));
				t.ival = std::strtoll(num.c_str(), nullptr, 10);
				t.is_long = long_suffix;
				toks.push_back(t);
			}
			continue;
		}
		// 字符字面量
		if (c == '\'') {
			advance();
			char v = 0;
			if (peek() == '\\') {
				advance();
				char e = advance();
				switch (e) {
					case 'n': v = '\n'; break;
					case 't': v = '\t'; break;
					case 'r': v = '\r'; break;
					case '0': v = '\0'; break;
					case '\\': v = '\\'; break;
					case '\'': v = '\''; break;
					case '"': v = '"'; break;
					default: v = e; break;
				}
			} else {
				v = advance();
			}
			if (peek() == '\'') advance();
			else errs_.push_back("字符字面量未闭合");
			Token t = make(TOK_CHARLIT, std::string(1, v));
			t.ival = (unsigned char)v;
			toks.push_back(t);
			continue;
		}
		// 字符串字面量
		if (c == '"') {
			advance();
			std::string s;
			while (peek() != '"' && peek() != '\0') {
				if (peek() == '\\') {
					advance();
					char e = advance();
					switch (e) {
						case 'n': s += '\n'; break;
						case 't': s += '\t'; break;
						case 'r': s += '\r'; break;
						case '0': s += '\0'; break;
						case '\\': s += '\\'; break;
						case '"': s += '"'; break;
						case '\'': s += '\''; break;
						default: s += e; break;
					}
				} else {
					s += advance();
				}
			}
			if (peek() == '"') advance();
			else errs_.push_back("字符串字面量未闭合");
			Token t = make(TOK_STRING, s);
			t.sval = s;
			toks.push_back(t);
			continue;
		}
		// 运算符
		advance();
		switch (c) {
			case '(': toks.push_back(make(TOK_LPAREN, "(")); break;
			case ')': toks.push_back(make(TOK_RPAREN, ")")); break;
			case '{': toks.push_back(make(TOK_LBRACE, "{")); break;
			case '}': toks.push_back(make(TOK_RBRACE, "}")); break;
			case '[': toks.push_back(make(TOK_LBRACKET, "[")); break;
			case ']': toks.push_back(make(TOK_RBRACKET, "]")); break;
			case ';': toks.push_back(make(TOK_SEMI, ";")); break;
			case ',': toks.push_back(make(TOK_COMMA, ",")); break;
			case '.':
if (peek() == '.' && peek(1) == '.') {
advance(); advance();
toks.push_back(make(TOK_ELLIPSIS, "..."));
} else {
toks.push_back(make(TOK_DOT, "."));
}
break;
			case '?': toks.push_back(make(TOK_QUESTION, "?")); break;
			case ':': toks.push_back(make(TOK_COLON, ":")); break;
			case '-':
				if (peek() == '>') { advance(); toks.push_back(make(TOK_ARROW, "->")); }
				else if (peek() == '-') { advance(); toks.push_back(make(TOK_DEC, "--")); }
				else if (peek() == '=') { advance(); toks.push_back(make(TOK_MINUS_ASSIGN, "-=")); }
				else toks.push_back(make(TOK_MINUS, "-"));
				break;
			case '~': toks.push_back(make(TOK_TILDE, "~")); break;
			case '=':
				if (peek() == '=') { advance(); toks.push_back(make(TOK_EQ, "==")); }
				else toks.push_back(make(TOK_ASSIGN, "="));
				break;
			case '+':
				if (peek() == '+') { advance(); toks.push_back(make(TOK_INC, "++")); }
				else if (peek() == '=') { advance(); toks.push_back(make(TOK_PLUS_ASSIGN, "+=")); }
				else toks.push_back(make(TOK_PLUS, "+"));
				break;
			case '*':
				if (peek() == '=') { advance(); toks.push_back(make(TOK_STAR_ASSIGN, "*=")); }
				else toks.push_back(make(TOK_STAR, "*"));
				break;
			case '/':
				if (peek() == '=') { advance(); toks.push_back(make(TOK_SLASH_ASSIGN, "/=")); }
				else toks.push_back(make(TOK_SLASH, "/"));
				break;
			case '%':
				if (peek() == '=') { advance(); toks.push_back(make(TOK_PERCENT_ASSIGN, "%=")); }
				else toks.push_back(make(TOK_PERCENT, "%"));
				break;
			case '!':
				if (peek() == '=') { advance(); toks.push_back(make(TOK_NE, "!=")); }
				else toks.push_back(make(TOK_NOT, "!"));
				break;
			case '<':
				if (peek() == '<') {
					advance();
					if (peek() == '=') { advance(); toks.push_back(make(TOK_SHL_ASSIGN, "<<=")); }
					else toks.push_back(make(TOK_SHL, "<<"));
				} else if (peek() == '=') { advance(); toks.push_back(make(TOK_LE, "<=")); }
				else toks.push_back(make(TOK_LT, "<"));
				break;
			case '>':
				if (peek() == '>') {
					advance();
					if (peek() == '=') { advance(); toks.push_back(make(TOK_SHR_ASSIGN, ">>=")); }
					else toks.push_back(make(TOK_SHR, ">>"));
				} else if (peek() == '=') { advance(); toks.push_back(make(TOK_GE, ">=")); }
				else toks.push_back(make(TOK_GT, ">"));
				break;
			case '&':
				if (peek() == '&') { advance(); toks.push_back(make(TOK_ANDAND, "&&")); }
				else if (peek() == '=') { advance(); toks.push_back(make(TOK_AND_ASSIGN, "&=")); }
				else toks.push_back(make(TOK_AND, "&"));
				break;
			case '|':
				if (peek() == '|') { advance(); toks.push_back(make(TOK_OROR, "||")); }
				else if (peek() == '=') { advance(); toks.push_back(make(TOK_OR_ASSIGN, "|=")); }
				else toks.push_back(make(TOK_OR, "|"));
				break;
			case '^':
				if (peek() == '=') { advance(); toks.push_back(make(TOK_XOR_ASSIGN, "^=")); }
				else toks.push_back(make(TOK_XOR, "^"));
				break;
			default:
				errs_.push_back("无法识别的字符: " + std::string(1, c));
				toks.push_back(make(TOK_EOF, ""));
				break;
		}
	}
	return toks;
}

} // namespace dcc
