// parser.cpp - dcc 递归下降语法分析器
#include "parser.h"
#include <cstdio>
#include <cstring>

namespace dcc {

Parser::Parser(const std::vector<Token>& toks, TypeEnv& env) : toks_(toks), cur_(0), env_(env) {}

const Token& Parser::peek() const { return toks_[cur_]; }
const Token& Parser::at(size_t off) const {
	size_t p = cur_ + off;
	return (p < toks_.size()) ? toks_[p] : toks_.back();
}
bool Parser::is(TokenKind k) const { return peek().kind == k; }
bool Parser::accept(TokenKind k) {
	if (is(k)) { cur_++; return true; }
	return false;
}
bool Parser::expect(TokenKind k, const std::string& what) {
	if (is(k)) { cur_++; return true; }
	error("期望 " + what, peek());
	return false;
}
const Token& Parser::advance() {
	const Token& t = toks_[cur_];
	if (t.kind != TOK_EOF) cur_++;
	return t;
}

void Parser::error(const std::string& msg, const Token& t) {
	errs_.push_back("第 " + std::to_string(t.line) + " 行: " + msg + " (near '" + t.text + "')");
}

// ---------------- 顶层 ----------------

const Type* Parser::var_type(const std::string& name) const {
	auto it = var_types_.find(name);
	return it == var_types_.end() ? nullptr : &it->second;
}

// 类型尺寸（含 struct/union 查 env、枚举按 int）
long long Parser::compute_size(const Type& t) {
	if (t.is_ptr()) return 4;
	switch (t.base) {
		case B_STRUCT:
		case B_UNION: {
			const StructDef* d = env_.lookup_struct(t.tname);
			return d ? (long long)d->size : 4;
		}
		case B_ENUM: return 4;
		case B_CHAR: return 1;
		case B_BOOL: return 1;
		case B_SHORT: return 2;
		case B_LONG: return 8;	// long 8 字节（64 位）
		default: return 4;
	}
}

// sizeof：编译期常量（E_INT）
Expr* Parser::parse_sizeof() {
	advance();	// sizeof
	Expr* e = new Expr;
	e->kind = E_INT;
	e->type = type_int();
	if (is(TOK_LPAREN)) {
		// sizeof(type) 或 sizeof(expr)
		size_t save = cur_;
		advance();
		// 尝试解析为类型
		if (is(TOK_INT) || is(TOK_SHORT) || is(TOK_LONG) || is(TOK_CHAR) || is(TOK_BOOL) || is(TOK_VOID) ||
		    is(TOK_SIGNED) || is(TOK_UNSIGNED) || is(TOK_CONST) || is(TOK_STRUCT) ||
		    is(TOK_UNION) || is(TOK_ENUM) || (is(TOK_IDENT) && env_.has_typedef(peek().text))) {
			Type t = parse_type();
			while (is(TOK_STAR)) { advance(); t.ptr_depth++; }
			if (accept(TOK_RPAREN)) {
				e->ival = compute_size(t);
				return e;
			}
			// 不是完整类型（如 sizeof(int) + 1 之类不会出现）；回退表达式
			cur_ = save;
		} else {
			cur_ = save;
		}
	}
	// sizeof expr：解析表达式求类型；数组变量取总大小
	Expr* inner = parse_unary();
	Type it = parser_expr_type(inner);
	if (inner->kind == E_VAR) {
		auto al = var_array_len_.find(inner->name);
		if (al != var_array_len_.end() && al->second > 0)
			e->ival = compute_size(it) * al->second;
		else
			e->ival = compute_size(it);
	} else {
		e->ival = compute_size(it);
	}
	delete inner;
	return e;
}

// 解析表达式类型（parser 侧）：E_VAR 查变量表；E_MEMBER 递归；E_INDEX 指针所指
Type Parser::parser_expr_type(Expr* e) {
	if (!e) return type_int();
	switch (e->kind) {
		case E_VAR: {
			const Type* t = var_type(e->name);
			return t ? *t : type_int();
		}
		case E_MEMBER:
			return e->type;		// 已解析的成员类型
		case E_INDEX: {
			Type bt = parser_expr_type(e->l);
			return bt.is_ptr() ? bt.pointee() : bt;
		}
		case E_STR:
			return type_str();
		case E_BINOP: {
			if (e->op == "&&" || e->op == "||") return type_int();
			Type lt = parser_expr_type(e->l);
			Type rt = parser_expr_type(e->r);
			if ((lt.is_ptr() || rt.is_ptr()) && (e->op == "+" || e->op == "-"))
				return lt.is_ptr() ? lt : rt;
			if (e->op == "==" || e->op == "!=" || e->op == "<" || e->op == "<=" ||
			    e->op == ">" || e->op == ">=") return type_int();
			if (lt.is_long() || rt.is_long())
				return (lt.is_unsigned || rt.is_unsigned) ? type_ulong() : type_long();
			return type_int();
		}
		default:
			return e->type;
	}
}

// 类型说明符：[signed|unsigned] (int|char|void|short|long) | typedef 名 | struct/union/enum
// 裸 char 按 unsigned（与 v0.1 一致）；裸 int 按 signed。const 修饰只读。
Type Parser::parse_type() {
	bool is_unsigned = false;
	bool saw_mod = false;
	bool is_const = false;
	if (is(TOK_CONST)) { advance(); is_const = true; }
	if (is(TOK_SIGNED)) { advance(); saw_mod = true; }
	if (is(TOK_UNSIGNED)) { advance(); saw_mod = true; is_unsigned = true; }
	// const 可出现在类型名后（int const x）
	auto apply_const = [&](Type t) { t.is_const = is_const; return t; };
	if (is(TOK_INT)) { advance(); return apply_const(Type(B_INT, is_unsigned, 0)); }
	if (is(TOK_SHORT)) { advance(); return apply_const(Type(B_SHORT, is_unsigned, 0)); }
	// long double
	if (is(TOK_LONG) && at(1).kind == TOK_DOUBLE) {
		advance(); advance();
		if (saw_mod) error("long double 不能带 signed/unsigned", peek());
		return apply_const(Type(B_LDOUBLE, false, 0));
	}
	if (is(TOK_LONG)) {
		advance();
		if (is(TOK_LONG)) { error("不支持 long long", peek()); advance(); }
		return apply_const(Type(B_LONG, is_unsigned, 0));	// long 8 字节（64 位）
	}
	// 浮点类型
	if (is(TOK_FLOAT)) {
		advance();
		if (saw_mod) error("float 不能带 signed/unsigned", peek());
		return apply_const(Type(B_FLOAT, false, 0));
	}
	if (is(TOK_DOUBLE)) {
		advance();
		if (saw_mod) error("double 不能带 signed/unsigned", peek());
		return apply_const(Type(B_DOUBLE, false, 0));
	}
	// 裸 char 按 unsigned（与 v0.1 一致）；signed char → signed
	if (is(TOK_CHAR)) { advance(); return apply_const(Type(B_CHAR, is_unsigned, 0)); }
	// C99 _Bool：1 字节无符号布尔（只存 0/1）
	if (is(TOK_BOOL)) {
		advance();
		if (saw_mod) error("_Bool 不能带 signed/unsigned", peek());
		return apply_const(Type(B_BOOL, true, 0));
	}
	if (is(TOK_VOID)) {
		advance();
		if (saw_mod) error("void 不能带 signed/unsigned", peek());
		return apply_const(type_void());
	}
	if (is(TOK_STRUCT) || is(TOK_UNION)) {
		bool is_union = is(TOK_UNION);
		advance();
		return apply_const(parse_struct_type(is_union));
	}
	if (is(TOK_ENUM)) {
		advance();
		return apply_const(parse_enum_type());
	}
	if (is(TOK_IDENT)) {	// typedef 名
		const Type* tt = env_.lookup_typedef(peek().text);
		if (tt) {
			advance();
			Type r = *tt;
			if (saw_mod) {
				if (r.is_int() || r.is_short() || r.is_char()) r.is_unsigned = is_unsigned;
				else error("类型修饰符与 typedef 类型不匹配", peek());
			}
			r.is_const = is_const;
			return r;
		}
	}
	if (saw_mod) {	// 单独 signed/unsigned → int
		return apply_const(Type(B_INT, is_unsigned, 0));
	}
	error("期望类型 int 或 char", peek());
	return type_int();
}

// 成员尺寸：数组 → 元素尺寸×个数；struct/union → 布局尺寸（查 env）；枚举 → 4
int Parser::member_type_size(const Type& t, int array_len) {
	int esz;
	if (t.is_ptr()) {
		esz = 4;	// 指针成员（含自引用 struct*）恒 4 字节
	} else if (t.base == B_STRUCT || t.base == B_UNION) {
		const StructDef* d = env_.lookup_struct(t.tname);
		esz = d ? (int)d->size : 4;	// 未定义（自引用）按指针已处理；此处兜底 4
	} else if (t.base == B_ENUM) {
		esz = 4;
	} else {
		esz = type_size(t);
	}
	return array_len > 0 ? esz * array_len : esz;
}

// 解析 struct/union：已消费 struct/union 关键字
//   struct S { ... } | struct S | struct { ... }
// 返回 struct/union 类型；若为定义则注册到 env
Type Parser::parse_struct_type(bool is_union) {
	std::string tag;
	if (is(TOK_IDENT)) { tag = advance().text; }
	// 匿名或仅引用：tag 可能为空或已定义
	if (is(TOK_LBRACE)) {
		advance();
		StructDef def;
		def.name = tag;
		def.is_union = is_union;
		uint32_t off = 0;
		uint32_t max_sz = 0;
		while (!is(TOK_RBRACE) && !is(TOK_EOF)) {
			Type mt = parse_type();
			std::string mn;
			if (!parse_declarator(mt, mn)) { error("期望成员名", peek()); break; }
			MemberDef md;
			md.name = mn;
			md.type = mt;
			md.is_array = false;
			md.array_len = 0;
			if (is(TOK_LBRACKET)) {	// 数组成员
				advance();
				if (!is(TOK_NUMBER)) { error("期望数组长度", peek()); return type_int(); }
				md.array_len = (int)advance().ival;
				md.is_array = true;
				if (!expect(TOK_RBRACKET, "']'")) return type_int();
			}
			int msz = member_type_size(mt, md.array_len);
			if (is_union) {
				md.offset = 0;					// union：共享起点
				if ((uint32_t)msz > max_sz) max_sz = (uint32_t)msz;
			} else {
				md.offset = off;
				off += (uint32_t)msz;
			}
			def.members.push_back(md);
			if (!accept(TOK_SEMI)) {
				error("结构体成员后缺少 ';'", peek());
				break;
			}
		}
		if (!expect(TOK_RBRACE, "'}'")) return type_int();
		def.size = is_union ? max_sz : off;
		if (def.name.empty()) {
			// 匿名 struct：生成唯一名
			static int anon_id = 0;
			def.name = "@anon" + std::to_string(anon_id++);
		}
		std::string def_name = def.name;	// 先保存（move 前）
		env_.add_struct(std::move(def));
		return Type(is_union ? B_UNION : B_STRUCT, false, 0, def_name);
	}
	// 仅引用已定义类型（struct S; 或 struct S x;）
	if (tag.empty()) { error("struct 缺少标签或定义", peek()); return type_int(); }
	if (!env_.has_struct(tag)) {
		// 未定义：允许指针引用（自引用/前向声明）；按值使用会在 codegen 报错
		return Type(is_union ? B_UNION : B_STRUCT, false, 0, tag);
	}
	return Type(is_union ? B_UNION : B_STRUCT, false, 0, tag);
}

// 解析 enum：已消费 enum 关键字
//   enum Color { RED, GREEN = 5, BLUE } | enum Color
Type Parser::parse_enum_type() {
	std::string tag;
	if (is(TOK_IDENT)) {
		// 仅引用（enum Color x;）——检查是否已定义
		if (!is(TOK_LBRACE) && at(1).kind == TOK_IDENT && at(2).kind == TOK_IDENT && at(3).kind == TOK_SEMI) {
			// 粗略：enum Name 后跟标识符且再后是 ; 或 [ —— 引用
		}
		tag = advance().text;
	}
	EnumDef def;
	def.name = tag;
	if (is(TOK_LBRACE)) {
		advance();
		long long next_val = 0;
		while (!is(TOK_RBRACE) && !is(TOK_EOF)) {
			if (!is(TOK_IDENT)) { error("期望枚举常量名", peek()); break; }
			std::string cname = advance().text;
			long long v = next_val;
			if (accept(TOK_ASSIGN)) {
				if (!is(TOK_NUMBER) && !is(TOK_IDENT)) { error("期望枚举值", peek()); break; }
				if (is(TOK_NUMBER)) v = advance().ival;
				else {
					// 引用其它枚举常量
					long long ev = env_.enum_value(peek().text);
					if (ev < 0) { error("未定义的枚举常量: " + peek().text, peek()); break; }
					v = ev;
					advance();
				}
			}
			def.constants.push_back({cname, v});
			next_val = v + 1;
			if (!accept(TOK_COMMA)) break;
		}
		if (!expect(TOK_RBRACE, "'}'")) return type_int();
		if (def.name.empty()) {
			static int anon_id = 0;
			def.name = "@enum" + std::to_string(anon_id++);
		}
		env_.add_enum(std::move(def));
	}
	// 枚举类型尺寸 = int
	return Type(B_ENUM, false, 0, tag);
}

// typedef 处理：已消费 typedef 关键字
bool Parser::parse_typedef() {
	Type t = parse_type();
	Type base_type = t;	// typedef int *A, *B; 时 B 从 int* 重新开始
	std::string name;
	if (!parse_declarator(t, name)) return true;
	if (is(TOK_LBRACKET)) {
		error("typedef 数组不支持", peek());
		// 跳过
		while (!is(TOK_SEMI) && !is(TOK_EOF)) advance();
		accept(TOK_SEMI);
		return true;
	}
	// 支持多个 typedef 名（typedef int A, B;）
	for (;;) {
		// 重复 typedef：类型相同允许（多文件/头文件场景），不同才报错
		const Type* old = env_.lookup_typedef(name);
		if (old && !type_same(*old, t)) {
			error("typedef 重复定义（类型不同）: " + name, peek());
			return true;
		}
		env_.add_typedef(name, t);
		if (!accept(TOK_COMMA)) break;
		t = base_type;
		if (!parse_declarator(t, name)) { error("期望 typedef 名", peek()); break; }
	}
	if (!expect(TOK_SEMI, "';'")) return true;
	return true;
}

// 声明符：解析 * 序列与名字；名字写入 out_name，指针深度叠加到 t
//   int *p;      → t 变成 int*
//   char **pp;   → t 变成 char**
//   void (*cb)(int); → t 变成函数指针类型，参数列表被 parse_declarator 消费
// 返回 false 表示语法错误（无名字）
bool Parser::parse_declarator(Type& t, std::string& out_name) {
	// 先消费外层 *（属于返回类型，如 int *(*fp)(...) 的 int *）
	while (is(TOK_STAR)) {
		advance();
		t.ptr_depth++;
	}

	// 函数指针形式：(*name)(参数) 或 (*name)()
	// 这里“(*”作为一个整体进入声明符；普通函数声明 `foo(` 不会被消费。
	if (is(TOK_LPAREN) && at(1).kind == TOK_STAR) {
		Type ret = t;			// 当前 t 是返回类型（可能已带外层 *）
		advance();				// (
		while (is(TOK_STAR)) {
			advance();	// 这是函数指针自身的“*”，不属于返回类型
		}
		if (!is(TOK_IDENT)) { error("期望函数指针名", peek()); return false; }
		out_name = advance().text;
		if (!expect(TOK_RPAREN, "')'")) return false;
		if (!is(TOK_LPAREN)) {
			error("函数指针声明缺少参数列表", peek());
			return false;
		}
		Type fp = type_func_ptr(ret, std::vector<Type>());
		parse_func_ptr_params(fp);
		t = fp;
		return true;
	}

	// 普通指针/变量声明
	if (!is(TOK_IDENT)) { error("期望标识符", peek()); return false; }
	out_name = advance().text;
	return true;
}

// 解析函数指针的参数列表（调用者已看到 '('；本函数消费 '(' 和 ')'）。
// 只记录参数类型；参数名可选，读取后丢弃。
void Parser::parse_func_ptr_params(Type& t) {
	if (!expect(TOK_LPAREN, "'('")) return;
	t.func_sig->params.clear();
	if (accept(TOK_RPAREN)) return;
	// (void) 表示无参数
	if (is(TOK_VOID)) {
		advance();
		if (accept(TOK_RPAREN)) return;
		error("函数指针参数 (void) 后应为 ')'", peek());
		// 继续按普通参数解析以恢复
	}
	for (;;) {
		Type pt = parse_type();
		while (is(TOK_STAR)) { advance(); pt.ptr_depth++; }
		if (is(TOK_IDENT)) advance();	// 参数名，忽略
		t.func_sig->params.push_back(pt);
		if (!accept(TOK_COMMA)) break;
	}
	if (!expect(TOK_RPAREN, "')'")) return;
}

void Parser::parse_global(Program& prog) {
	// static：文件作用域内部链接
	bool is_static = false;
	if (is(TOK_STATIC)) { advance(); is_static = true; }
	// inline：记录标记，用于内联优化
	bool is_inline = false;
	if (is(TOK_INLINE)) { advance(); is_inline = true; }
	// typedef 定义（注册到 env，无变量/函数产生）
	if (is(TOK_TYPEDEF)) {
		advance();
		parse_typedef();
		return;
	}
	// extern 修饰符（变量声明/函数原型）
	bool is_extern = false;
	if (is(TOK_EXTERN)) { advance(); is_extern = true; }

	Type t = parse_type();
	Type base_type = t;	// 逗号分隔声明时，后续声明符从基础类型重新开始
	// struct/union/enum 纯定义（无声明符，如 `struct S {...};` / `enum E {...};`）
	if (is(TOK_SEMI) && (t.base == B_STRUCT || t.base == B_UNION || t.base == B_ENUM)) {
		advance();
		return;
	}
	std::string name;
	if (!parse_declarator(t, name)) return;

	// 函数
	if (is(TOK_LPAREN)) {
		advance();
		Function fn;
		fn.name = name;
		fn.ret_type = t;
		fn.local_size = 0;
		fn.is_decl = false;
		fn.is_isr = false;
		fn.is_inline = is_inline;
		fn.is_vararg = false;
		fn.is_static = is_static;
		if (is(TOK_VOID) && at(1).kind == TOK_RPAREN) {	// (void)：空参数列表
			advance();
			if (!expect(TOK_RPAREN, "')'")) return;
			if (is(TOK_INTERRUPT)) {
				advance();
				fn.is_isr = true;
				if (t.base != B_NONE) error("__interrupt__ 函数必须返回 void", peek());
			}
			if (is_extern) fn.is_decl = true;
			finish_func_or_proto(fn, t, name, prog);
			return;
		}
		if (!is(TOK_RPAREN)) {
			for (;;) {
				// 可变参数：..., 必须在至少一个命名参数之后
				if (is(TOK_ELLIPSIS)) {
					advance();
					if (fn.params.empty()) error("可变参数 '...' 前至少需要有一个命名参数", peek());
					fn.is_vararg = true;
					break;
				}
				Type pt = parse_type();
				std::string pn;
				if (!parse_declarator(pt, pn)) { error("期望参数名", peek()); break; }
				fn.param_names.push_back(pn);
				fn.params.push_back(pt);
				var_types_[pn] = pt;	// 记录参数类型
				if (!accept(TOK_COMMA)) break;
				if (is(TOK_ELLIPSIS)) {	// 逗号后的 ...
					advance();
					fn.is_vararg = true;
					break;
				}
			}
		}
		if (!expect(TOK_RPAREN, "')'")) return;
		// __interrupt__ 修饰符：ISR（无参数、返回 void、return 编为 IRET）
		if (is(TOK_INTERRUPT)) {
			advance();
			fn.is_isr = true;
			if (!fn.params.empty()) error("__interrupt__ 函数不能有参数", peek());
			if (t.base != B_NONE) error("__interrupt__ 函数必须返回 void", peek());
		}
		if (is_extern) fn.is_decl = true;	// extern 函数声明 = 原型
		finish_func_or_proto(fn, t, name, prog);
		return;
	}

	// 全局变量（可多个，逗号分隔；每个变量可带各自指针/数组声明）
	for (;;) {
		GlobalVar gv;
		gv.type = t;
		gv.is_array = false;
		gv.array_len = 0;
		gv.ival_init = 0;
		gv.has_init = false;
		gv.label_init.clear();
		gv.has_str_init = false;
		gv.offset = 0;
		gv.label = "var_" + name;
		gv.name = name;
		gv.is_extern = is_extern;
		gv.is_static = is_static;

		if (is(TOK_LBRACKET)) {	// 数组
			advance();
			if (!is(TOK_NUMBER)) { error("期望数组长度", peek()); return; }
			long long len = advance().ival;
			gv.is_array = true;
			gv.array_len = (int)len;
			if (!expect(TOK_RBRACKET, "']'")) return;
		}
		if (accept(TOK_ASSIGN)) {
			if (gv.is_extern) { error("extern 变量不能有初始化器", peek()); return; }
			if (is(TOK_STRING)) {
				gv.has_str_init = true;
				gv.str_init = advance().sval;
			} else {
				Expr* e = parse_assign();
				if (e && e->kind == E_FLOAT) {
					float f = (float)e->fval;
					uint32_t bits;
					memcpy(&bits, &f, sizeof(bits));
					gv.ival_init = bits;
				}
				else if (e && e->kind == E_INT) {
					gv.ival_init = t.is_bool() ? (e->ival != 0 ? 1 : 0) : e->ival;
				}
				else if (e && e->kind == E_STR) {	// char* g = "abc"
					gv.has_str_init = true;
					gv.str_init = e->name;
				}
				else if (e && e->kind == E_VAR && t.is_func_ptr_type()) {
					// 函数指针全局初始化：void (*fp)(void) = foo;
					gv.label_init = "func_" + e->name;
				}
				else if (e && e->kind == E_UNOP && e->op == "&" &&
				         e->r && e->r->kind == E_VAR && t.is_func_ptr_type()) {
					// 函数指针全局初始化：void (*fp)(void) = &foo;
					gv.label_init = "func_" + e->r->name;
				}
				else error("全局初始化器仅支持常量", peek());
				delete e;
				gv.has_init = true;
			}
		}
		prog.globals.push_back(gv);
		var_types_[name] = t;	// 记录全局变量类型（成员访问解析用）
		var_array_len_[name] = gv.is_array ? gv.array_len : 0;	// sizeof 用
		if (!accept(TOK_COMMA)) break;
		// 逗号后的声明符从基础类型重新解析，避免把 int *a, *b 的 b 解析成 int**
		t = base_type;
		if (!parse_declarator(t, name)) { error("期望标识符", peek()); return; }
	}
	if (!expect(TOK_SEMI, "';'")) return;
}

Program Parser::parse() {
	Program prog;
	while (!is(TOK_EOF)) {
		if (is(TOK_EXTERN) || is(TOK_STATIC) || is(TOK_INLINE) || is(TOK_INT) || is(TOK_SHORT) || is(TOK_LONG) || is(TOK_FLOAT) || is(TOK_DOUBLE) || is(TOK_CHAR) || is(TOK_BOOL) || is(TOK_VOID) ||
		    is(TOK_SIGNED) || is(TOK_UNSIGNED) || is(TOK_CONST) || is(TOK_STRUCT) || is(TOK_UNION) ||
		    is(TOK_ENUM) || is(TOK_TYPEDEF) || is(TOK_IDENT)) {
			size_t before = cur_;
			parse_global(prog);
			// 语法错误时避免顶层死循环：若 parse_global 未消费任何 token，则跳过
			if (cur_ == before && !is(TOK_EOF)) advance();
		}
		else { error("期望类型声明（int/char）", peek()); advance(); }
	}
	return prog;
}

// ---------------- 函数 ----------------

// 函数声明收尾：`{` → 定义（parse_func）；`;` → 原型（is_decl）
void Parser::finish_func_or_proto(Function& fn, Type ret, const std::string& name, Program& prog) {
	if (is(TOK_LBRACE)) {
		if (fn.is_decl) { error("extern 函数不能有函数体", peek()); return; }
		parse_func(fn, ret, name);
	} else if (is(TOK_SEMI)) {
		advance();
		fn.is_decl = true;
	} else {
		error("期望 '{' 或 ';'", peek());
		return;
	}
	prog.funcs.push_back(fn);
}

void Parser::parse_func(Function& fn, Type ret, const std::string& name) {
	(void)ret; (void)name;	// 已在 parse_global 中登记到 fn
	// 注意：不在此消费 '{'，parse_block 自己 expect(TOK_LBRACE)
	Stmt* body = parse_block();
	if (body && body->kind == S_BLOCK) {
		for (Stmt* s : body->items) fn.body.push_back(s);
		body->items.clear();	// 所有权转移到 fn.body，避免析构重复释放
		delete body;
	}
}

// ---------------- 变量声明 ----------------

VarDecl Parser::parse_var_decl(Type t, const std::string& name) {
	VarDecl d;
	d.name = name;
	d.type = t;
	d.is_array = false;
	d.array_len = 0;
	d.init = nullptr;
	d.has_str_init = false;
	d.is_static = false;
	if (is(TOK_LBRACKET)) {
		advance();
		if (!is(TOK_NUMBER)) { error("期望数组长度", peek()); return d; }
		long long len = advance().ival;
		d.is_array = true;
		d.array_len = (int)len;
		if (!expect(TOK_RBRACKET, "']'")) return d;
	}
	if (accept(TOK_ASSIGN)) {
		if (is(TOK_STRING)) {
			if (d.is_array) {
				// char s[n] = "abc"：字符串拷贝
				d.has_str_init = true;
				d.str_init = advance().sval;
			} else {
				// char *p = "abc"：指针指向字符串常量
				Expr* e = new Expr;
				e->kind = E_STR;
				e->name = advance().sval;
				e->type = type_str();
				d.init = e;
			}
		} else {
			d.init = parse_assign();
		}
	}
	return d;
}

// ---------------- 语句 ----------------

// __asm__("dasmasm 文本");  —— GNU 风格内联汇编（无操作数约束）
// 支持相邻字符串字面量拼接；文本为多行 DASM。
Stmt* Parser::parse_asm() {
	advance();	// __asm__
	if (!expect(TOK_LPAREN, "'('")) return nullptr;
	Stmt* s = new Stmt;
	s->kind = S_ASM;
	// 拼接相邻字符串字面量
	for (;;) {
		if (!is(TOK_STRING)) {
			error("__asm__ 期望字符串字面量", peek());
			delete s;
			return nullptr;
		}
		s->asm_text += advance().sval;
		if (!is(TOK_STRING)) break;
	}
	if (!expect(TOK_RPAREN, "')'")) { delete s; return nullptr; }
	// GNU 允许省略分号（asm 后换行），dcc 要求显式分号
	if (!accept(TOK_SEMI)) {
		error("__asm__ 语句后缺少 ';'", peek());
		delete s;
		return nullptr;
	}
	return s;
}

Stmt* Parser::parse_stmt() {
	if (is(TOK_LBRACE)) return parse_block();
	if (is(TOK_RETURN)) {
		advance();
		Stmt* s = new Stmt;
		s->kind = S_RETURN;
		if (!is(TOK_SEMI)) s->expr = parse_expr();
		if (!expect(TOK_SEMI, "';'")) { delete s; return nullptr; }
		return s;
	}
	if (is(TOK_IF)) return parse_if();
	if (is(TOK_WHILE)) return parse_while();
	if (is(TOK_FOR)) return parse_for();
	if (is(TOK_BREAK)) {
		advance();
		Stmt* s = new Stmt;
		s->kind = S_BREAK;
		if (!expect(TOK_SEMI, "';'")) { delete s; return nullptr; }
		return s;
	}
	if (is(TOK_CONTINUE)) {
		advance();
		Stmt* s = new Stmt;
		s->kind = S_CONTINUE;
		if (!expect(TOK_SEMI, "';'")) { delete s; return nullptr; }
		return s;
	}
	if (is(TOK_ASM)) return parse_asm();
	bool local_static = false;
	if (is(TOK_STATIC)) {
		advance();
		local_static = true;
	}
	if (is(TOK_INT) || is(TOK_SHORT) || is(TOK_LONG) || is(TOK_FLOAT) || is(TOK_DOUBLE) || is(TOK_CHAR) || is(TOK_BOOL) || is(TOK_VOID) || is(TOK_SIGNED) || is(TOK_UNSIGNED) || is(TOK_CONST) ||
	    is(TOK_STRUCT) || is(TOK_UNION) || is(TOK_ENUM) || is(TOK_TYPEDEF) ||
	    (is(TOK_IDENT) && env_.has_typedef(peek().text))) {
		// 局部 typedef 定义（仅注册 env，不产生代码）
		if (is(TOK_TYPEDEF)) {
			advance();
			parse_typedef();
			return nullptr;
		}
		Type base = parse_type();
		Type t = base;
		std::string nm;
		if (!parse_declarator(t, nm)) return nullptr;
		return parse_decl_stmt(base, t, nm, local_static);
	}
	// 表达式语句
	Stmt* s = new Stmt;
	s->kind = S_EXPR;
	s->expr = parse_expr();
	if (!expect(TOK_SEMI, "';'")) { delete s; return nullptr; }
	return s;
}

Stmt* Parser::parse_decl_stmt(Type base, Type t, const std::string& name, bool is_static) {
	// 支持逗号分隔的多变量声明：int a, b, c; 或 int *p, *q;
	// 每个变量生成一个 S_DECL 语句，用 next 链接（codegen 按顺序处理）
	Stmt* first = nullptr;
	Stmt* last = nullptr;
	Type cur_t = t;
	std::string nm = name;
	for (;;) {
		VarDecl d = parse_var_decl(cur_t, nm);
		d.is_static = is_static;
		var_types_[d.name] = d.type;	// 记录局部变量类型（成员访问解析用）
		var_array_len_[d.name] = d.is_array ? d.array_len : 0;	// sizeof 用
		Stmt* s = new Stmt;
		s->kind = S_DECL;
		s->decl = d;
		if (!first) first = s;
		if (last) last->next = s;
		last = s;
		if (!accept(TOK_COMMA)) break;
		// 逗号后：重新解析声明符（可带 *），类型继承声明头 base
		cur_t = base;
		if (!parse_declarator(cur_t, nm)) { error("期望变量名", peek()); break; }
	}
	if (!expect(TOK_SEMI, "';'")) {
		// 出错时释放已建语句
		delete first;
		return nullptr;
	}
	return first;
}

Stmt* Parser::parse_block() {
	if (!expect(TOK_LBRACE, "'{'")) return nullptr;
	Stmt* s = new Stmt;
	s->kind = S_BLOCK;
	while (!is(TOK_RBRACE) && !is(TOK_EOF)) {
		Stmt* sub = parse_stmt();
		if (!sub) {
			// 语法错误时避免死循环：跳过当前 token，继续尝试恢复
			if (!is(TOK_RBRACE) && !is(TOK_EOF)) advance();
			continue;
		}
		// 展开逗号分隔声明链（next）
		while (sub) {
			Stmt* nx = sub->next;
			sub->next = nullptr;
			s->items.push_back(sub);
			sub = nx;
		}
	}
	if (!expect(TOK_RBRACE, "'}'")) { delete s; return nullptr; }
	return s;
}

Stmt* Parser::parse_if() {
	advance();	// if
	Stmt* s = new Stmt;
	s->kind = S_IF;
	if (!expect(TOK_LPAREN, "'('")) { delete s; return nullptr; }
	s->cond = parse_expr();
	if (!expect(TOK_RPAREN, "')'")) { delete s; return nullptr; }
	s->then = parse_stmt();
	if (accept(TOK_ELSE)) s->els = parse_stmt();
	return s;
}

Stmt* Parser::parse_while() {
	advance();	// while
	Stmt* s = new Stmt;
	s->kind = S_WHILE;
	if (!expect(TOK_LPAREN, "'('")) { delete s; return nullptr; }
	s->cond = parse_expr();
	if (!expect(TOK_RPAREN, "')'")) { delete s; return nullptr; }
	s->body = parse_stmt();
	return s;
}

Stmt* Parser::parse_for() {
	advance();	// for
	Stmt* s = new Stmt;
	s->kind = S_FOR;
	if (!expect(TOK_LPAREN, "'('")) { delete s; return nullptr; }

	// C99 风格：for (int i = 0; ...; ...)
	bool init_is_decl =
	    is(TOK_INT) || is(TOK_SHORT) || is(TOK_LONG) || is(TOK_FLOAT) || is(TOK_DOUBLE) || is(TOK_CHAR) || is(TOK_BOOL) ||
	    is(TOK_VOID) || is(TOK_SIGNED) || is(TOK_UNSIGNED) || is(TOK_CONST) ||
	    is(TOK_STRUCT) || is(TOK_UNION) || is(TOK_ENUM) || is(TOK_TYPEDEF) ||
	    (is(TOK_IDENT) && env_.has_typedef(peek().text));
	if (init_is_decl) {
		Type base = parse_type();
		Type t = base;
		std::string nm;
		if (!parse_declarator(t, nm)) { delete s; return nullptr; }
		s->init_stmt = parse_decl_stmt(base, t, nm, false);
		if (!s->init_stmt) { delete s; return nullptr; }
		// parse_decl_stmt 已经消费了 for 的第一个 ';'
	} else {
		if (!is(TOK_SEMI)) s->init = parse_expr();
		if (!expect(TOK_SEMI, "';'")) { delete s; return nullptr; }
	}

	if (!is(TOK_SEMI)) s->cond = parse_expr();
	if (!expect(TOK_SEMI, "';'")) { delete s; return nullptr; }
	if (!is(TOK_RPAREN)) s->inc = parse_expr();
	if (!expect(TOK_RPAREN, "')'")) { delete s; return nullptr; }
	s->body = parse_stmt();
	return s;
}

// ---------------- 表达式 ----------------

// 当前位置是 '(' 且其后紧跟类型说明符 → 强制类型转换
bool Parser::is_cast_start() const {
	if (!is(TOK_LPAREN)) return false;
	if (cur_ + 1 >= toks_.size()) return false;
	TokenKind k = toks_[cur_ + 1].kind;
	if (k == TOK_INT || k == TOK_SHORT || k == TOK_LONG || k == TOK_CHAR || k == TOK_BOOL || k == TOK_VOID ||
	    k == TOK_SIGNED || k == TOK_UNSIGNED || k == TOK_CONST ||
	    k == TOK_STRUCT || k == TOK_UNION || k == TOK_ENUM) return true;
	// typedef 名转换，如 (uint32_t)5
	if (k == TOK_IDENT) {
		const Type* tt = env_.lookup_typedef(toks_[cur_ + 1].text);
		if (tt) return true;
	}
	return false;
}

int Parser::binop_prec(const Token& t) const {
	switch (t.kind) {
		case TOK_OROR: return 1;
		case TOK_ANDAND: return 2;
		case TOK_OR: return 3;
		case TOK_XOR: return 4;
		case TOK_AND: return 5;
		case TOK_EQ: case TOK_NE: return 6;
		case TOK_LT: case TOK_LE: case TOK_GT: case TOK_GE: return 7;
		case TOK_SHL: case TOK_SHR: return 8;
		case TOK_PLUS: case TOK_MINUS: return 9;
		case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return 10;
		default: return 0;
	}
}

Expr* Parser::parse_expr() { return parse_assign(); }

Expr* Parser::parse_assign() {
	Expr* lhs = parse_cond();
	if (!lhs) return nullptr;
	TokenKind op = peek().kind;
	bool is_assign = false;
	switch (op) {
		case TOK_ASSIGN: case TOK_PLUS_ASSIGN: case TOK_MINUS_ASSIGN:
		case TOK_STAR_ASSIGN: case TOK_SLASH_ASSIGN: case TOK_PERCENT_ASSIGN:
		case TOK_AND_ASSIGN: case TOK_OR_ASSIGN: case TOK_XOR_ASSIGN:
		case TOK_SHL_ASSIGN: case TOK_SHR_ASSIGN:
			is_assign = true;
			break;
		default: break;
	}
	if (!is_assign) return lhs;
	Expr* e = new Expr;
	e->kind = E_ASSIGN;
	e->l = lhs;
	e->op = advance().text;
	e->r = parse_assign();
	e->type = e->l->type;
	return e;
}

Expr* Parser::parse_cond() {
	Expr* c = parse_binary(1);
	if (!c) return nullptr;
	if (!is(TOK_QUESTION)) return c;
	advance();
	Expr* e = new Expr;
	e->kind = E_COND;
	e->c = c;
	e->l = parse_expr();
	if (!expect(TOK_COLON, "':'")) { delete e; return nullptr; }
	e->r = parse_cond();
	e->type = e->l->type;
	return e;
}

Expr* Parser::parse_binary(int min_prec) {
	Expr* lhs = parse_unary();
	if (!lhs) return nullptr;
	for (;;) {
		int prec = binop_prec(peek());
		if (prec < min_prec) break;
		std::string op = advance().text;
		Expr* rhs = parse_binary(prec + 1);
		Expr* e = new Expr;
		e->kind = E_BINOP;
		e->op = op;
		e->l = lhs;
		e->r = rhs;
		// 类型：指针 ± 整数 → 指针；指针比较 → int；其余整数提升
		if ((lhs->type.is_ptr() || rhs->type.is_ptr()) && (op == "+" || op == "-")) {
			e->type = lhs->type.is_ptr() ? lhs->type : rhs->type;
		} else {
			e->type = type_int();
		}
		lhs = e;
	}
	return lhs;
}

Expr* Parser::parse_unary() {
	if (is(TOK_SIZEOF)) return parse_sizeof();
	if (is(TOK_MINUS) || is(TOK_NOT) || is(TOK_TILDE)) {
		std::string op = advance().text;
		Expr* e = new Expr;
		e->kind = E_UNOP;
		e->op = op;
		e->r = parse_unary();
		e->type = type_int();	// 整数提升
		return e;
	}
	if (is(TOK_AND)) {		// &x：取地址
		advance();
		Expr* e = new Expr;
		e->kind = E_UNOP;
		e->op = "&";
		e->r = parse_unary();
		if (e->r) e->type = type_ptr(e->r->type);	// 指向操作数类型
		else e->type = type_ptr(type_int());
		return e;
	}
	if (is(TOK_STAR)) {		// *p：解引用
		advance();
		Expr* e = new Expr;
		e->kind = E_UNOP;
		e->op = "*";
		e->r = parse_unary();
		if (e->r && e->r->type.is_ptr()) {
			// 函数指针解引用在 C 中仍表示函数指示符，调用时等价于函数地址
			if (e->r->type.is_func_ptr_type()) e->type = e->r->type;
			else e->type = e->r->type.pointee();
		}
		else e->type = type_int();
		return e;
	}
	if (is(TOK_INC) || is(TOK_DEC)) {
		std::string op = advance().text;
		Expr* e = new Expr;
		e->kind = E_INCDEC;
		e->op = op;
		e->postfix = false;
		e->r = parse_unary();
		e->type = e->r ? e->r->type : type_int();
		return e;
	}
	return parse_postfix();
}

Expr* Parser::parse_postfix() {
	Expr* e = parse_primary();
	if (!e) return nullptr;
	for (;;) {
		if (is(TOK_LBRACKET)) {		// 下标
			advance();
			Expr* idx = parse_expr();
			if (!expect(TOK_RBRACKET, "']'")) { delete e; return nullptr; }
			Expr* n = new Expr;
			n->kind = E_INDEX;
			n->l = e;
			n->r = idx;
			// 元素类型：数组 → 元素类型；指针 → 指针所指类型
			if (e->type.is_ptr()) n->type = e->type.pointee();
			else n->type = e->type;
			e = n;
			continue;
		}
		if (is(TOK_DOT) || is(TOK_ARROW)) {	// 成员访问 s.field / p->field
			bool arrow = is(TOK_ARROW);
			advance();
			if (!is(TOK_IDENT)) { error("期望成员名", peek()); delete e; return nullptr; }
			std::string mname = advance().text;
			Expr* n = new Expr;
			n->kind = E_MEMBER;
			n->l = e;
			n->arrow = arrow;
			n->member = mname;
			// 成员类型：解析基表达式类型（E_VAR 查 var_types_；嵌套成员递归）
			Type bt = parser_expr_type(e);
			if (arrow) bt = bt.pointee();
			if (bt.base == B_STRUCT || bt.base == B_UNION) {
				const StructDef* sd = env_.lookup_struct(bt.tname);
				const MemberDef* md = nullptr;
				if (sd) for (const MemberDef& m : sd->members) if (m.name == mname) { md = &m; break; }
				if (md) {
					// 成员类型；数组成员视为指向元素的指针（数组名用法）
					n->type = md->is_array ? type_ptr(md->type) : md->type;
				}
				else error("结构体无此成员: " + mname, peek());
			} else {
				error("成员访问对象不是结构体", peek());
			}
			e = n;
			continue;
		}
		if (is(TOK_LPAREN)) {		// 调用
			advance();
			Expr* n = new Expr;
			n->kind = E_CALL;
			n->l = e;			// 保留被调表达式：直接函数名或函数指针
			n->name = (e && e->kind == E_VAR) ? e->name : std::string();
			n->type = type_int();	// 默认；codegen 会查函数表/函数指针类型修正
			if (e) {
				Type ct = parser_expr_type(e);
				if (ct.is_func_ptr_type()) n->type = ct.func_ret_type();
			}
			if (!is(TOK_RPAREN)) {
				for (;;) {
					n->args.push_back(parse_assign());
					if (!accept(TOK_COMMA)) break;
				}
			}
			if (!expect(TOK_RPAREN, "')'")) { delete n; return nullptr; }
			e = n;
			continue;
		}
		if (is(TOK_INC) || is(TOK_DEC)) {	// 后缀
			std::string op = advance().text;
			Expr* n = new Expr;
			n->kind = E_INCDEC;
			n->op = op;
			n->postfix = true;
			n->r = e;
			n->type = e->type;
			e = n;
			continue;
		}
		break;
	}
	return e;
}

Expr* Parser::parse_primary() {
	if (is(TOK_NUMBER)) {
		Expr* e = new Expr;
		e->kind = E_INT;
		e->ival = advance().ival;
		e->type = type_int();
		return e;
	}
	if (is(TOK_FLOATLIT)) {
		Token t = advance();
		Expr* e = new Expr;
		e->kind = E_FLOAT;
		e->fval = t.fval;
		// f/F 后缀 → float；无后缀或 l/L → double/long double 按 double 处理
		e->type = (!t.text.empty() && (t.text.back() == 'f' || t.text.back() == 'F')) ? type_float() : type_double();
		return e;
	}
	if (is(TOK_CHARLIT)) {
		Expr* e = new Expr;
		e->kind = E_INT;
		e->ival = advance().ival;
		e->type = type_char();
		return e;
	}
	if (is(TOK_STRING)) {
		Expr* e = new Expr;
		e->kind = E_STR;
		e->name = advance().sval;
		e->type = type_str();		// char*（指向字符串常量）
		return e;
	}
	// 强制类型转换 (type)expr，如 (int)c、(char)x、(unsigned int)*p、(char*)s
	if (is_cast_start()) {
		advance();					// '('
		Type t = parse_type();
		while (is(TOK_STAR)) { advance(); t.ptr_depth++; }	// 指针类型 (int*) 等
		if (!expect(TOK_RPAREN, "')'")) return nullptr;
		Expr* e = new Expr;
		e->kind = E_CAST;
		e->type = t;
		e->r = parse_unary();		// 转换作用于一个一元表达式
		return e;
	}
	if (is(TOK_IDENT)) {
		std::string txt = advance().text;
		// __reg_ 前缀 → 寄存器直访（A/B/C/D1/D2/X/I/S/R/F/T；E 只能作右值）
		if (txt.size() > 6 && txt.compare(0, 6, "__reg_") == 0) {
			std::string reg = txt.substr(6);
			bool ok = (reg == "A" || reg == "B" || reg == "C" ||
			           reg == "D1" || reg == "D2" || reg == "X" || reg == "I" ||
			           reg == "S" || reg == "R" || reg == "F" || reg == "T" ||
			           reg == "E");
			if (ok) {
				Expr* e = new Expr;
				e->kind = E_REGDIR;
				e->name = reg;
				e->type = type_uint();	// 寄存器按 unsigned int 解释（位模式）
				e->lvalue = (reg != "E");	// E 只能作为右值
				return e;
			}
			error("未知寄存器: " + txt, peek());
		}
		// 枚举常量 → 整数字面量
		long long ev = env_.enum_value(txt);
		if (ev >= 0) {
			Expr* e = new Expr;
			e->kind = E_INT;
			e->ival = ev;
			e->type = type_int();
			return e;
		}
		Expr* e = new Expr;
		e->kind = E_VAR;
		e->name = txt;
		e->type = type_int();		// 占位；codegen 查符号表修正
		e->lvalue = true;
		return e;
	}
	if (is(TOK_LPAREN)) {
		advance();
		Expr* e = parse_expr();
		if (!expect(TOK_RPAREN, "')'")) { delete e; return nullptr; }
		return e;
	}
	error("期望表达式", peek());
	return nullptr;
}

} // namespace dcc
