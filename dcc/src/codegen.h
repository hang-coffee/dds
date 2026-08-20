// codegen.h - dcc 代码生成（C AST → DOCTOR dasm 汇编文本）
#ifndef DCC_CODEGEN_H
#define DCC_CODEGEN_H

#include "ast.h"
#include "typeenv.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace dcc {

class CodeGen {
public:
	// 生成完整 .asm 文本；失败返回空串并在 errors 中记录
	std::string generate(Program& prog, const TypeEnv& env);
void set_include_start(bool b) { include_start_ = b; }
	const std::vector<std::string>& errors() const { return errs_; }

private:
	const TypeEnv* env_ = nullptr;		// 用户定义类型环境（struct/enum/typedef）
	std::vector<std::string> errs_;
	SymTable symtab_;			// 全局+局部作用域符号
	std::unordered_map<std::string, Function*> funcs_;	// 函数表（解析用）
	std::unordered_map<std::string, Type> func_ret_;	// 返回值类型

	std::string text_;			// TEXT 段输出
	std::string data_;			// DATA 段输出
	uint32_t data_off_;			// 全局数据偏移
	int label_cnt_;				// 标号计数
	int str_cnt_;				// 字符串常量计数
	std::string cur_func_;		// 当前函数标号
	int cur_func_named_bytes_ = 0;	// 当前函数命名参数总槽位字节（可变参数 va_start 用）
	bool cur_isr_ = false;		// 当前函数是否为 __interrupt__（return → IRET）
	bool cur_ret_long_ = false;	// 当前函数返回类型是否为 long（返回值留在 A:D1）
	bool cur_ret_bool_ = false;	// 当前函数返回类型是否为 _Bool（返回值归一化为 0/1）
	bool cur_ret_fp_ = false;	// 当前函数返回类型是否为浮点（结果在 FP0）
	bool cur_ret_double_ = false;	// 当前函数返回类型是否为 double/long double
	bool include_start_ = false;	// 是否生成 _start 启动代码（已移至外部 CRT，默认不生成）
	// 循环栈（break/continue 目标）
	struct LoopCtx { std::string brk, cont; };
	std::vector<LoopCtx> loops_;

	void emit_t(const std::string& s);
	void emit_d(const std::string& s);
	std::string new_label();
	std::string new_str_label();

	// 顶层
	void gen_global(GlobalVar& g);
	void gen_func(Function& f);
	void gen_start();
	void emit_str_data(const std::string& label, const std::string& s);	// 每字节一行 DB

	// 语句
	void gen_stmt(Stmt* s);
	void gen_decl(Stmt* s);

	// 表达式（结果在 A）
	void gen_expr(Expr* e);
	void gen_lvalue_addr(Expr* e);			// 左值地址 → B
	void gen_assign(Expr* e);
	void gen_binop(Expr* e);
	void gen_binop_long(Expr* e);		// 64 位 long 运算（+ - * / % << >> 比较）
	void emit_d1_signext();				// D1 = A 的符号扩展（int → long 提升）
	void emit_bool_normalize();			// A → 0/1（C99 _Bool 赋值/返回转换）
	void emit_fp_load_a_to_fp0();		// A 中的 32 位浮点位模式 → FP0
	void emit_fp_store_fp0_to_a();		// FP0 → A（32 位浮点位模式）
	void gen_fp_expr(Expr* e);			// 求浮点表达式，结果在 FP0
	void emit_fp_binop(const std::string& op);	// FP0 = FP0 op FP1
	void emit_dp_load_a_d1_to_dp0();		// A:D1 的 64 位浮点位模式 → DP0
	void emit_dp_store_dp0_to_a_d1();		// DP0 → A:D1（64 位浮点位模式）
	void gen_dp_expr(Expr* e);			// 求 double 表达式，结果在 DP0
	void emit_dp_binop(const std::string& op);	// DP0 = DP0 op DP1
	// 64 位 long 运算辅助。约定：A=低 32 位, D1=高 32 位；
	// 二元运算的右操作数在 B(低)/R(高)。
	void emit_long_add();				// A:D1 += B:R（含进位）
	void emit_long_sub();				// A:D1 -= B:R（含借位）
	void emit_long_mul();				// A:D1 = A:D1 * B:R（低 64 位，学校算法）
	void emit_long_neg();				// A:D1 = -(A:D1)（64 位取负）
	void emit_long_not();				// A:D1 = ~(A:D1)
	void emit_long_udiv();				// 无符号 A:D1 / B:R → 商 A:D1、余 C:D2（移位减法）
	void emit_long_divmod(bool want_rem, bool uns);	// 除法入口（含符号处理）
	void emit_long_shift(const std::string& op, int v, bool uns);	// A:D1 <<= v / >>= v
	void emit_long_cmp(const std::string& op, bool uns,
	                   const std::string& ltrue, const std::string& lfalse);	// (D1:A) 比较 (R:B) 后跳转
	void gen_compare(Expr* e, const std::string& jump);	// C 已=A-B
	void gen_shortcircuit(Expr* e, bool is_and);
	void gen_call(Expr* e);
bool function_inlinable(const Function* f) const;
void gen_inline_call(Expr* e, const Function* f);
	void gen_cond(Expr* e);					// A 的值 → 0/1（逻辑真值），供 if/while
	void emit_store(Type t);				// ST 到 [B]，按类型

	// 类型辅助（v0.2）
	bool type_is_char(const Type& t) const;	// 1 字节（char/unsigned char，非指针）
	void emit_load_from_a(Type t);			// LR 到 A（[A] 为地址），signed char 符号扩展
	void emit_store_to_b(Type t);			// ST 从 A 到 [B]
	void emit_binop_signed_div();			// A/B（signed）→ D2 商、D1 余（内联符号处理）
	int pointee_size(const Type& t) const;	// 指针所指类型尺寸（*p 一次步进）
	Type resolve_type(Expr* e);				// 递归解析表达式类型（查符号表）
	void emit_scale(int sz);				// A *= sz（sz=1/2/4/8 用 SHL；否则临时寄存器）
	int tsize(const Type& t) const;			// 类型尺寸（struct/enum 查 env）
	int member_offset(const std::string& struct_name, const std::string& member) const;
	const MemberDef* member_def(const std::string& struct_name, const std::string& member) const;

	// 变量地址/读取
	void gen_var_addr_to_a(const Symbol& s);
	void gen_var_addr_to_b(const Symbol& s);
	Type lvalue_type(Expr* e);			// 左值表达式类型（查符号表）

	const Symbol* lookup_var(const std::string& name, int line);
	bool is_const_int(Expr* e, long long& v);
	void error(const std::string& msg);
};

} // namespace dcc

#endif
