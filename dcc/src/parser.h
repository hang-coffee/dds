// parser.h - dcc 递归下降语法分析器
#ifndef DCC_PARSER_H
#define DCC_PARSER_H

#include "token.h"
#include "ast.h"
#include "typeenv.h"
#include <vector>
#include <string>

namespace dcc {

class Parser {
public:
	explicit Parser(const std::vector<Token>& toks, TypeEnv& env);
	Program parse();
	const std::vector<std::string>& errors() const { return errs_; }

private:
	const std::vector<Token>& toks_;
	size_t cur_;
	std::vector<std::string> errs_;
	TypeEnv& env_;
	std::unordered_map<std::string, Type> var_types_;	// 变量名 → 类型（成员访问解析用）
	std::unordered_map<std::string, int> var_array_len_;	// 变量名 → 数组长度（sizeof 用）
	const Type* var_type(const std::string& name) const;
	Type parser_expr_type(Expr* e);		// 表达式类型（E_VAR 查 var_types_；嵌套成员）
	long long compute_size(const Type& t);	// 类型尺寸（struct 查 env）
	Expr* parse_sizeof();				// sizeof(type) / sizeof expr → 编译期常量

	const Token& peek() const;
	const Token& at(size_t off) const;
	bool is(TokenKind k) const;
	bool accept(TokenKind k);
	bool expect(TokenKind k, const std::string& what);
	const Token& advance();

	// 顶层
	void parse_global(Program& prog);
	Type parse_type();		// [signed|unsigned] int|char|void|short|typedef|struct|union|enum 后跟 * 序列
	Type parse_named_type();	// 解析一个基础类型说明符（含 struct/union/enum/typedef）
	void parse_struct_def(bool is_union);	// 解析 struct/union 定义并注册 env
	Type parse_struct_type(bool is_union);	// 已消费 struct/union；返回类型
	Type parse_enum_type();					// 已消费 enum；返回类型
	int member_type_size(const Type& t, int array_len);	// 成员尺寸（struct 查 env）
	bool parse_typedef();	// 处理 typedef 声明；返回是否有内容
	// 解析声明符（指针 * 序列 + 名字；名字写入 out_name，指针深度计入 type）
	bool parse_declarator(Type& t, std::string& out_name);
	void parse_func_ptr_params(Type& t);	// 解析函数指针参数列表（如 (*cb)(int, char)）
	void parse_func(Function& fn, Type ret, const std::string& name);
	void finish_func_or_proto(Function& fn, Type ret, const std::string& name, Program& prog);
	void parse_global_var(GlobalVar& gv, Type t, const std::string& name);

	// 语句
	Stmt* parse_stmt();
	Stmt* parse_block();
	Stmt* parse_decl_stmt(Type base, Type t, const std::string& name, bool is_static = false);
	VarDecl parse_var_decl(Type t, const std::string& name);
	Stmt* parse_asm();				// __asm__("dasmasm");
	Stmt* parse_if();
	Stmt* parse_while();
	Stmt* parse_for();

	// 表达式（优先级爬升）
	Expr* parse_expr();
	Expr* parse_assign();
	Expr* parse_cond();
	Expr* parse_binary(int min_prec);
	Expr* parse_unary();
	Expr* parse_postfix();
	Expr* parse_primary();
	bool is_cast_start() const;		// 当前位置 '(' 后跟类型说明符（强制转换）
	int binop_prec(const Token& t) const;

	void error(const std::string& msg, const Token& t);
};

} // namespace dcc

#endif
