// ast.h - dcc 语法树与符号表
#ifndef DCC_AST_H
#define DCC_AST_H

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace dcc {

// ---- 类型系统（v0.3）----
// 基础类型 + 符号性 + 指针深度 + 用户定义类型名。
//   int*          → base=B_INT,  is_unsigned=false, ptr_depth=1
//   unsigned int  → base=B_INT,  is_unsigned=true,  ptr_depth=0
//   short         → base=B_SHORT, is_unsigned=false, ptr_depth=0（2 字节）
//   char*         → base=B_CHAR, is_unsigned=true,  ptr_depth=1（字符串等）
//   struct S      → base=B_STRUCT, tname="S", ptr_depth=0
//   enum Color    → base=B_ENUM,  tname="Color", ptr_depth=0（int 尺寸）
// 指针本身总是 4 字节、按无符号地址处理。
// long 是 8 字节：表达式中值 = A(低 32 位) + D1(高 32 位)。
enum BaseType { B_NONE, B_INT, B_LONG, B_SHORT, B_CHAR, B_STRUCT, B_UNION, B_ENUM };

struct Type {
	BaseType base;
	bool is_unsigned;
	bool is_const;		// const 限定（只读）
	int ptr_depth;		// 0 = 非指针
	std::string tname;	// B_STRUCT/B_UNION/B_ENUM 的标签名（空 = 匿名）

	Type() : base(B_NONE), is_unsigned(false), is_const(false), ptr_depth(0) {}
	Type(BaseType b, bool u, int p) : base(b), is_unsigned(u), is_const(false), ptr_depth(p) {}
	Type(BaseType b, bool u, int p, const std::string& tn) : base(b), is_unsigned(u), is_const(false), ptr_depth(p), tname(tn) {}

	bool is_ptr() const { return ptr_depth > 0; }
	bool is_void() const { return base == B_NONE && ptr_depth == 0; }
	bool is_char() const { return base == B_CHAR && ptr_depth == 0; }
	bool is_short() const { return base == B_SHORT && ptr_depth == 0; }
	bool is_int() const { return base == B_INT && ptr_depth == 0; }
	bool is_long() const { return base == B_LONG && ptr_depth == 0; }
	bool is_aggregate() const { return ptr_depth == 0 && (base == B_STRUCT || base == B_UNION); }
	bool is_struct() const { return ptr_depth == 0 && base == B_STRUCT; }
	bool is_union() const { return ptr_depth == 0 && base == B_UNION; }
	bool is_enum() const { return ptr_depth == 0 && base == B_ENUM; }
	bool is_unsigned_scalar() const { return ptr_depth == 0 && is_unsigned; }
	bool is_signed_scalar() const { return ptr_depth == 0 && !is_unsigned && base != B_NONE; }

	// 解引用后的类型（*p 的结果）；非指针时原样
	Type pointee() const {
		Type t = *this;
		if (t.ptr_depth > 0) t.ptr_depth--;
		return t;
	}
	// 剥离所有指针后的基础类型（决定元素尺寸/符号）
	Type base_of() const {
		Type t = *this;
		t.ptr_depth = 0;
		return t;
	}
};

// 常用类型快捷构造
inline Type type_void()   { return Type(B_NONE, false, 0); }
inline Type type_int()    { return Type(B_INT, false, 0); }
inline Type type_uint()   { return Type(B_INT, true, 0); }
inline Type type_long()   { return Type(B_LONG, false, 0); }
inline Type type_ulong()  { return Type(B_LONG, true, 0); }
inline Type type_short()  { return Type(B_SHORT, false, 0); }
inline Type type_ushort() { return Type(B_SHORT, true, 0); }
inline Type type_char()   { return Type(B_CHAR, false, 0); }
inline Type type_uchar()  { return Type(B_CHAR, true, 0); }
inline Type type_str()    { return Type(B_CHAR, true, 1); }	// char*（字符串）
inline Type type_ptr(const Type& pointee) {
	Type t = pointee;
	t.ptr_depth++;
	return t;
}

// 尺寸：指针/ int 4 字节；long 8 字节；short 2 字节；char 1 字节；
// struct/union 由布局决定（此函数不查 env，调用方需用 TypeEnv 查询）
inline int type_size(const Type& t) {
	if (t.ptr_depth > 0) return 4;
	switch (t.base) {
		case B_CHAR:  return 1;
		case B_SHORT: return 2;
		case B_LONG:  return 8;
		case B_STRUCT:
		case B_UNION:
		case B_ENUM:  return 0;	// 由 TypeEnv 布局/按 int
		default:      return 4;
	}
}

// 尺寸对应的 dasm 尺寸关键字（long 8 字节分两次 DWORD）
inline const char* type_size_word(const Type& t) {
	switch (type_size(t)) {
		case 1: return "BYTE";
		case 2: return "WORD";
		default: return "DWORD";
	}
}

// 类型是否相同（typedef 重复定义判定用）
inline bool type_same(const Type& a, const Type& b) {
	return a.base == b.base && a.is_unsigned == b.is_unsigned &&
	       a.ptr_depth == b.ptr_depth && a.tname == b.tname;
}

// ---- 表达式 ----
enum ExprKind {
	E_INT,			// 整数字面量
	E_STR,			// 字符串字面量（数据区标号）
	E_VAR,			// 变量（含数组名）
	E_REGDIR,		// 寄存器直访 __reg_A 等（name=寄存器名）
	E_CAST,			// 强制类型转换 (type)expr
	E_ASSIGN,		// a = b（含复合赋值）
	E_BINOP,		// 二元运算
	E_UNOP,			// 一元运算（- ~ !）
	E_INCDEC,		// ++ / --
	E_CALL,			// 函数调用
	E_INDEX,		// 数组下标 a[i]
	E_MEMBER,		// 成员访问 s.field / p->field
	E_COND,			// 三目 a ? b : c
};

struct Expr;
typedef std::vector<Expr*> ExprList;

struct Expr {
	ExprKind kind;
	Type type;				// 结果类型
	std::string name;		// 变量/函数名
	std::string member;		// E_MEMBER 成员名
	bool arrow;				// E_MEMBER: true=->（指针成员）
	long long ival;			// 字面量值
	std::string op;			// 运算符文本（E_BINOP/E_UNOP/E_ASSIGN/E_INCDEC）
	bool postfix;			// 后缀 ++/--
	Expr *l, *r, *c;		// 子表达式
	ExprList args;			// 调用实参
	bool lvalue;			// 是否为左值

	Expr() : kind(E_INT), type(type_int()), name(), member(), arrow(false), ival(0),
	         op(), postfix(false), l(nullptr), r(nullptr), c(nullptr), lvalue(false) {}
	~Expr();
};

// ---- 语句 ----
enum StmtKind {
	S_EXPR, S_RETURN, S_IF, S_WHILE, S_FOR, S_BLOCK, S_DECL, S_BREAK, S_CONTINUE,
	S_ASM,	// 内联汇编 __asm__("dasmasm")
};

struct VarDecl {
	std::string name;
	Type type;
	bool is_array;
	int array_len;			// 数组元素个数（0 = 非数组）
	Expr *init;				// 初始化器（标量）
	std::string str_init;	// 字符串初始化（char 数组）
	bool has_str_init;
};

struct Stmt {
	StmtKind kind;
	Expr *expr;				// S_EXPR / S_RETURN
	Expr *cond;				// S_IF / S_WHILE / S_FOR
	Stmt *then, *els;		// S_IF
	Stmt *body;				// S_WHILE / S_FOR
	Expr *init;				// S_FOR 初始化表达式
	Expr *inc;				// S_FOR 增量
	std::vector<Stmt*> items;	// S_BLOCK / S_DECL（var 声明序列）
	VarDecl decl;			// S_DECL
	Stmt *next;				// 逗号分隔的多变量声明链（S_DECL 用）
	std::string asm_text;	// S_ASM 内联汇编文本（dasm）

	Stmt() : kind(S_EXPR), expr(nullptr), cond(nullptr), then(nullptr),
	         els(nullptr), body(nullptr), init(nullptr), inc(nullptr), next(nullptr) {}
	~Stmt();
};

// ---- 函数 ----
struct Function {
	std::string name;
	Type ret_type;
	std::vector<Type> params;
	std::vector<std::string> param_names;
	std::vector<Stmt*> body;	// 函数体语句
	int local_size;				// 帧内局部空间大小（codegen 填充）
	bool is_decl;				// 函数原型（无函数体，不生成代码）
	bool is_isr;				// __interrupt__ 中断服务函数（return → IRET）
};

// ---- 全局变量 ----
struct GlobalVar {
	std::string name;
	Type type;
	bool is_array;
	int array_len;
	long long ival_init;		// 标量初始化
	bool has_init;
	std::string str_init;		// char 数组字符串初始化
	bool has_str_init;
	int offset;					// data 区偏移（codegen 填充）
	std::string label;			// var_<name>
	bool is_extern;				// extern 声明（不分配存储，由其它编译单元定义）
};

// ---- 程序（顶层）：全局变量 + 函数 ----
struct Program {
	std::vector<GlobalVar> globals;
	std::vector<Function> funcs;
};

// ---- 符号（作用域内）----
struct Symbol {
	std::string name;
	Type type;
	bool is_array;
	int array_len;
	bool is_global;
	bool is_arg;				// 函数参数（帧内偏移为 F 下方距离）
	uint32_t offset;			// 全局: data 偏移; 局部: F+offset; 参数: F-offset
	std::string label;			// 全局标号
};

// 符号表：作用域栈
class SymTable {
public:
	void push_scope();
	void pop_scope();
	bool declare(const Symbol& s);			// 当前作用域声明（重名报错）
	const Symbol* lookup(const std::string& name) const;
private:
	std::vector<std::unordered_map<std::string, Symbol>> scopes_;
};

} // namespace dcc

#endif
