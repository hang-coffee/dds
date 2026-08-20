// codegen.cpp - dcc 代码生成实现
//
// 生成 DOCTOR dasm 汇编。调用约定见 docs/calling-convention.md：
//   - 参数经栈传递（从左到右压入），返回地址最后压入；调用方清栈
//   - 帧指针 F（SFA 建立），局部变量在 F+4..；参数在 F 下方
//   - 返回值寄存器 D1（return 时 MOV D1, A；调用后 MOV A, D1）
//   - 表达式求值：结果在 A；右操作数先压栈、左操作数在 A、POP 到 B
//   - 条件跳转寄存器操作数：比较用 C 与 X=0
//   - char 按无符号处理（0-255，v0.1）

#include "codegen.h"

#include <cstdio>
#include <cstring>
#include <functional>

namespace dcc {

void CodeGen::emit_t(const std::string& s) { text_ += "\t" + s + "\n"; }
void CodeGen::emit_d(const std::string& s) { data_ += "\t" + s + "\n"; }
std::string CodeGen::new_label() {
	return "L" + std::to_string(label_cnt_++);
}
std::string CodeGen::new_str_label() {
	return "str" + std::to_string(str_cnt_++);
}

// 生成字符串常量数据：每字节一行 `DB <addr>, 0xXX`（dasm 的 DB 只支持单值）
void CodeGen::emit_str_data(const std::string& label, const std::string& s) {
	data_ += label + ":\n";
	size_t n = s.size() + 1;	// +NUL
	for (size_t i = 0; i < n; i++) {
		unsigned v = (i < s.size()) ? (unsigned char)s[i] : 0;
		char buf[16];
		snprintf(buf, sizeof(buf), "\tDB %u, 0x%02X", data_off_, v);
		data_ += buf;
		data_ += "\n";
		data_off_++;
	}
}

void CodeGen::error(const std::string& msg) {
	errs_.push_back(msg);
}

bool CodeGen::is_const_int(Expr* e, long long& v) {
	if (e && e->kind == E_INT) { v = e->ival; return true; }
	return false;
}

const Symbol* CodeGen::lookup_var(const std::string& name, int line) {
	const Symbol* s = symtab_.lookup(name);
	if (!s) error("第 " + std::to_string(line) + " 行: 未定义变量 '" + name + "'");
	return s;
}

// 左值类型：E_VAR 查符号表；E_INDEX 查基数组/指针元素类型；*p 用指针所指；其余用表达式类型
Type CodeGen::lvalue_type(Expr* e) {
	if (!e) return type_int();
	if (e->kind == E_VAR) {
		const Symbol* s = symtab_.lookup(e->name);
		if (s) return s->type;
		return type_int();
	}
	if (e->kind == E_INDEX) {
		if (e->l && e->l->kind == E_VAR) {
			const Symbol* s = symtab_.lookup(e->l->name);
			if (s && s->is_array) return s->type;		// 数组元素类型
			if (s && s->type.is_ptr()) return s->type.pointee();	// 指针元素
		}
		if (e->l && e->l->type.is_ptr()) return e->l->type.pointee();
		return type_int();
	}
	if (e->kind == E_UNOP && e->op == "*") {
		if (e->r && e->r->type.is_ptr()) return e->r->type.pointee();
		return type_int();
	}
	if (e->kind == E_MEMBER) {
		// 成员类型；数组成员返回元素类型（下标/读写按元素尺寸）
		Type bt = resolve_type(e->l);
		if (e->arrow) bt = bt.pointee();
		const MemberDef* md = member_def(bt.tname, e->member);
		if (md && md->is_array) return md->type;
		return e->type;
	}
	if (e->kind == E_REGDIR) return type_uint();	// 寄存器是 unsigned int
	return e->type;
}

// ---------------- 顶层 ----------------

std::string CodeGen::generate(Program& prog, const TypeEnv& env) {
	env_ = &env;
	label_cnt_ = 0;
	str_cnt_ = 0;
	data_off_ = 0;
	text_.clear();
	data_.clear();

	// 函数表（用于调用解析）
	for (Function& f : prog.funcs) {
		funcs_["func_" + f.name] = &f;
		func_ret_["func_" + f.name] = f.ret_type;
	}

	// 全局变量 → DATA
	for (GlobalVar& g : prog.globals) {
		g.label = "var_" + g.name;
		if (g.is_extern) {
			// extern 声明：不分配存储；登记符号（label 指向定义者，
			// 若最终无定义则 dasm 报未定义标号）
			Symbol s;
			s.name = g.name;
			s.type = g.type;
			s.is_array = g.is_array;
			s.array_len = g.array_len;
			s.is_global = true;
			s.is_arg = false;
			s.offset = 0;
			s.label = g.label;
			symtab_.declare(s);
			continue;
		}
		g.offset = data_off_;
		// 登记全局符号（函数内可引用）
		Symbol s;
		s.name = g.name;
		s.type = g.type;
		s.is_array = g.is_array;
		s.array_len = g.array_len;
		s.is_global = true;
		s.is_arg = false;
		s.offset = (uint32_t)g.offset;
		s.label = g.label;
		symtab_.declare(s);
		gen_global(g);
	}
	// 字符串常量由 gen_global/gen_decl 惰性产生（str_ 前缀追加到 data_）

	// 入口 _start 不再由 dcc 生成，用户自行链接 bootable_crt.asm / bin_crt.asm
	// 函数 → TEXT
	for (Function& f : prog.funcs) gen_func(f);

	// 组装
	std::string out;
	out += "\tSECTION DATA\n";
	out += "\tORG 0\n";
	out += data_;
	out += "\tSECTION TEXT\n";
	out += "\tORG 0\n";
	out += text_;
	return out;
}

void CodeGen::gen_global(GlobalVar& g) {
	// extern 声明：不分配存储（由定义该变量的编译单元生成数据）
	if (g.is_extern) return;
	// 标签
	data_ += g.label + ":\n";
	if (g.is_array) {
		int size = g.array_len * tsize(g.type);
		if (g.has_str_init) {
			// char 数组字符串初始化：先逐字节 DB，再 RESB 补齐
			size_t n = g.str_init.size() + 1;	// +NUL
			if (n > (size_t)g.array_len) n = g.array_len;
			std::string line;
			for (size_t i = 0; i < n; i++) {
				unsigned v = (i < g.str_init.size()) ? (unsigned char)g.str_init[i] : 0;
				char buf[16];
				snprintf(buf, sizeof(buf), "\tDB %u, 0x%02X", (unsigned)(g.offset + i), v);
				data_ += buf;
				data_ += "\n";
			}
			if (n < (size_t)size) {
				data_ += "\tRESB " + std::to_string(size - n) + "\n";
			}
			data_off_ += size;
		} else {
			data_ += "\tRESB " + std::to_string(size) + "\n";
			data_off_ += size;
		}
		return;
	}
	// 标量
	if (g.has_str_init) {
		// char *g = "abc"：var_gmsg 占 4 字节（DD 指向后置的字符串常量）
		std::string src = new_str_label();
		data_ += "\tDD " + std::to_string(g.offset) + ", " + src + "\n";
		data_off_ += 4;
		emit_str_data(src, g.str_init);	// 字符串常量紧跟其后（前向引用）
	} else if (g.has_init) {
		int sz = tsize(g.type);
		if (g.type.is_fp()) {
			uint32_t bits = (uint32_t)g.ival_init;
			char b[64];
			snprintf(b, sizeof(b), "\tDD %u, 0x%08X", (unsigned)g.offset, bits);
			data_ += b; data_ += "\n";
			if (sz == 8) {
				snprintf(b, sizeof(b), "\tDD %u, 0x00000000", (unsigned)(g.offset + 4));
				data_ += b; data_ += "\n";
			}
		} else if (!g.label_init.empty()) {
			// 函数指针/地址标量：DD <offset>, <label>
			data_ += "\tDD " + std::to_string(g.offset) + ", " + g.label_init + "\n";
		} else if (sz == 1) {
			data_ += "\tDB " + std::to_string(g.offset) + ", 0x" +
			         [](long long v){ char b[8]; snprintf(b,8,"%02llX",v&0xff); return std::string(b); }(g.ival_init) + "\n";
		} else if (sz == 2) {
			data_ += "\tDW " + std::to_string(g.offset) + ", " +
			         std::to_string(g.ival_init) + "\n";
		} else if (sz == 8) {
			// long：两个 DWORD（低字在前，小端序）
			uint32_t lo = (uint32_t)g.ival_init;
			uint32_t hi = (uint32_t)((uint64_t)g.ival_init >> 32);
			char b[64];
			snprintf(b, sizeof(b), "\tDD %u, 0x%08X", (unsigned)g.offset, lo);
			data_ += b; data_ += "\n";
			snprintf(b, sizeof(b), "\tDD %u, 0x%08X", (unsigned)(g.offset + 4), hi);
			data_ += b; data_ += "\n";
		} else {
			data_ += "\tDD " + std::to_string(g.offset) + ", " +
			         std::to_string(g.ival_init) + "\n";
		}
		data_off_ += tsize(g.type);
	} else {
		data_ += "\tRESB " + std::to_string(tsize(g.type)) + "\n";
		data_off_ += tsize(g.type);
	}
}

void CodeGen::gen_func(Function& f) {
	// 函数原型：仅登记函数表（generate 已做），不生成代码
	if (f.is_decl) return;
	cur_func_ = "func_" + f.name;
	cur_isr_ = f.is_isr;	// ISR：return → MOV S,F + IRET
	// 注意：不重置 label_cnt_，保证全程序标号唯一
	symtab_.push_scope();

	// 收集局部变量（递归遍历函数体所有声明），分配帧偏移
	int frame = 4;	// F+4 起
	std::function<void(std::vector<Stmt*>&)> collect =
	    [&](std::vector<Stmt*>& items) {
		for (Stmt* st : items) {
			if (st->kind == S_DECL) {
				for (Stmt* d = st; d; d = d->next) {
					VarDecl& dd = d->decl;
					int size = dd.is_array ? dd.array_len * tsize(dd.type)
					                       : tsize(dd.type);
					Symbol sym;
					sym.name = dd.name;
					sym.type = dd.type;
					sym.is_array = dd.is_array;
					sym.array_len = dd.array_len;
					sym.is_global = false;
					sym.is_arg = false;
					sym.offset = frame;
					sym.label = "";
					if (!symtab_.declare(sym)) {
						error("函数 '" + f.name + "' 中变量重复声明: " + dd.name);
					}
					frame += size;
				}
			}
			if (st->kind == S_BLOCK) collect(st->items);
			if (st->kind == S_IF) {
				if (st->then) { std::vector<Stmt*> v{st->then}; collect(v); }
				if (st->els) { std::vector<Stmt*> v{st->els}; collect(v); }
			}
			if (st->kind == S_WHILE && st->body) {
				std::vector<Stmt*> v{st->body}; collect(v);
			}
			if (st->kind == S_FOR) {
				if (st->init_stmt) { std::vector<Stmt*> v{st->init_stmt}; collect(v); }
				if (st->body) { std::vector<Stmt*> v{st->body}; collect(v); }
			}
		}
	};
	collect(f.body);
	// 局部变量从 F+4 起（F 处是返回地址）。SFA 的 N 是"局部空间字节数"，
	// 必须覆盖 [F+4, F+frame)：N = frame - 4 + 4 = frame？不——
	// PUSH 从 S+1 起写，SFA 后 S=F+N；为让临时 PUSH 不覆盖局部变量，
	// 需要 S 至少到 F+frame，即 N = frame - 4 + 4 = frame - 4 只到 F+frame-4？
	// 实测：N = frame-4 时 S=F+frame-4，临时 PUSH 写 [F+frame-3..F+frame]，
	// 与最后一个局部变量 [F+frame-4..F+frame-1] 的末尾重叠。
	// 正确：N = frame - 4（局部区大小本身），但局部区应从 F+4 起算，
	// 且栈顶 S 必须 ≥ 局部区顶端 F+frame。SFA 只做 S+=N，
	// 因此 N = frame（涵盖 F..F+frame，多出 4 字节冗余也无妨）。
	f.local_size = frame - 4;

	// 函数头
	emit_t(cur_func_ + ":");
	if (f.is_isr) {
		// ISR：先保存被中断代码的 F（中断入口硬件不保存 F）
		emit_t("PUSH DWORD F");
	}
	emit_t("SFA DWORD " + std::to_string(frame));

	// 参数符号表（真实参数名）
	// 布局：PUSH F → 参数(左→右：a 先压，地址低) → 返回地址（SFA 后 F 指向其后）
	// PUSH DWORD 语义是 S++; *S=value（数据从 S+1 起，整体错位 1 字节）：
	//   参数 i 首地址 = F - (3 + Σ_{j>=i} slot(j))，slot(long)=8、其余=4
	cur_ret_long_ = f.ret_type.is_long();
	cur_ret_bool_ = f.ret_type.is_bool();
	cur_ret_fp_ = f.ret_type.is_fp();
cur_ret_double_ = f.ret_type.is_double() || f.ret_type.is_ldouble();
	int n = (int)f.params.size();
	cur_func_named_bytes_ = 0;
	for (const auto& pt : f.params)
		cur_func_named_bytes_ += (pt.is_long() || pt.is_double() || pt.is_ldouble() ? 8 : 4);
	uint32_t acc = 0;			// 从右到左累计槽位字节
	for (int i = n - 1; i >= 0; i--) {
		Symbol s;
		s.name = (i < (int)f.param_names.size()) ? f.param_names[i] : ("arg" + std::to_string(i));
		s.type = f.params[i];
		s.is_array = false;
		s.array_len = 0;
		s.is_global = false;
		s.is_arg = true;
		acc += (f.params[i].is_long() || f.params[i].is_double() || f.params[i].is_ldouble() ? 8u : 4u);
		s.offset = acc + 3;
		symtab_.declare(s);
	}

	// 函数体
	for (Stmt* s : f.body) gen_stmt(s);

	// 尾声：普通函数返回 A（未显式 return 时）；ISR 恢复栈与 F 后 IRET
	if (f.is_isr) {
		emit_t("MOV S, F");		// S 指向保存的 F
		emit_t("POP DWORD F");	// 恢复被中断代码的 F
		emit_t("IRET");			// 弹 int_no, P, CTRL, ICTB
	} else {
		// long 返回值已在 A(低):D1(高)，直接返回
		if (!cur_ret_long_ && !cur_ret_double_) emit_t("MOV D1, A");
		emit_t("RER");
		emit_t("JMP");
	}

	symtab_.pop_scope();
}

void CodeGen::gen_start() {
	emit_t("_start:");
	emit_t("LET S, DWORD 0x300000");
	emit_t("MOV A, F");
	emit_t("PUSH DWORD A");		// 保存调用方 F（初始 0）
	std::string ret = "RET" + std::to_string(label_cnt_++);
	emit_t("LET E, DWORD " + ret);
	emit_t("PUSH DWORD E");
	emit_t("LET E, DWORD func_main");
	emit_t("JMP");
	emit_t(ret + ":");
	emit_t("POP DWORD F");
	// 主程序结束后进入停机-等待中断循环；中断 IRET 后回到这里继续 HLT，
	// 避免执行到 TEXT 段后续函数代码造成“重启/跑飞”。
	std::string halt = new_label();
	emit_t(halt + ":");
	emit_t("HLT");
	emit_t("LET E, DWORD " + halt);
	emit_t("JMP");
}

// ---------------- 语句 ----------------

void CodeGen::gen_stmt(Stmt* s) {
	if (!s) return;
	switch (s->kind) {
		case S_EXPR:
			if (s->expr) gen_expr(s->expr);
			break;
		case S_RETURN: {
			if (s->expr) {
				gen_expr(s->expr);
				// 浮点返回
				if (cur_ret_fp_) {
					if (cur_ret_double_) {
						// 当前函数返回 double/long double
						if (resolve_type(s->expr).is_double() || resolve_type(s->expr).is_ldouble()) {
							// DP0 已就绪
						} else if (resolve_type(s->expr).is_float()) {
							emit_t("F2D DP0, FP0");
						} else {
							emit_t("I2D DP0, A");
						}
						emit_dp_store_dp0_to_a_d1();
					} else {
						if (!resolve_type(s->expr).is_fp()) emit_t("I2F FP0, A");
						emit_fp_store_fp0_to_a();
					}
				} else if (resolve_type(s->expr).is_fp()) {
					// 浮点表达式返回给整数函数：转成整数 A
					if (resolve_type(s->expr).is_double() || resolve_type(s->expr).is_ldouble()) emit_t("D2I A, DP0");
					else emit_t("F2I A, FP0");
				}
				// _Bool 返回：归一化为 0/1
				if (cur_ret_bool_) emit_bool_normalize();
				// long 返回：int 表达式先提升为 long
				if (cur_ret_long_ && !resolve_type(s->expr).is_long()) {
					if (resolve_type(s->expr).is_unsigned_scalar()) emit_t("ZERO D1");
					else emit_d1_signext();
				}
			}
			if (cur_isr_) {
				// ISR 返回：恢复栈与 F 后 IRET
				emit_t("MOV S, F");
				emit_t("POP DWORD F");
				emit_t("IRET");
			} else {
				if (!cur_ret_long_ && !cur_ret_double_) emit_t("MOV D1, A");
				emit_t("RER");
				emit_t("JMP");
			}
			break;
		}
		case S_IF: {
			std::string lelse = new_label(), lend = new_label();
			gen_cond(s->cond);
			emit_t("MOV C, A");
			emit_t("ZERO T");
			emit_t("CMP DWORD T");
			emit_t("LET E, DWORD " + lelse);
			emit_t("JZ");
			gen_stmt(s->then);
			if (s->els) {
				emit_t("LET E, DWORD " + lend);
				emit_t("JMP");
				emit_t(lelse + ":");
				gen_stmt(s->els);
				emit_t(lend + ":");
			} else {
				emit_t(lelse + ":");
			}
			break;
		}
		case S_WHILE: {
			std::string lcond = new_label(), lend = new_label();
			loops_.push_back({lend, lcond});
			emit_t(lcond + ":");
			gen_cond(s->cond);
			emit_t("MOV C, A");
			emit_t("ZERO T");
			emit_t("CMP DWORD T");
			emit_t("LET E, DWORD " + lend);
			emit_t("JZ");
			gen_stmt(s->body);
			emit_t("LET E, DWORD " + lcond);
			emit_t("JMP");
			emit_t(lend + ":");
			loops_.pop_back();
			break;
		}
		case S_FOR: {
			std::string lcond = new_label(), linc = new_label(), lend = new_label();
			if (s->init_stmt) gen_stmt(s->init_stmt);
			if (s->init) gen_expr(s->init);
			loops_.push_back({lend, linc});
			emit_t(lcond + ":");
			if (s->cond) {
				gen_cond(s->cond);
				emit_t("MOV C, A");
				emit_t("ZERO T");
				emit_t("CMP DWORD T");
				emit_t("LET E, DWORD " + lend);
				emit_t("JZ");
			}
			gen_stmt(s->body);
			emit_t(linc + ":");
			if (s->inc) gen_expr(s->inc);
			emit_t("LET E, DWORD " + lcond);
			emit_t("JMP");
			emit_t(lend + ":");
			loops_.pop_back();
			break;
		}
		case S_BLOCK:
			for (Stmt* sub : s->items) gen_stmt(sub);
			break;
		case S_DECL:
			for (Stmt* d = s; d; d = d->next) gen_decl(d);
			break;
		case S_ASM: {
			// 内联汇编：按行原样嵌入 TEXT 段（空行跳过）
			size_t pos = 0;
			while (pos < s->asm_text.size()) {
				size_t nl = s->asm_text.find('\n', pos);
				std::string line = s->asm_text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
				if (!line.empty()) emit_t(line);
				if (nl == std::string::npos) break;
				pos = nl + 1;
			}
			break;
		}
		case S_BREAK:
			if (loops_.empty()) error("break 不在循环内");
			else { emit_t("LET E, DWORD " + loops_.back().brk); emit_t("JMP"); }
			break;
		case S_CONTINUE:
			if (loops_.empty()) error("continue 不在循环内");
			else { emit_t("LET E, DWORD " + loops_.back().cont); emit_t("JMP"); }
			break;
	}
}

// 局部变量声明：标量初始化/数组字符串初始化生成赋值代码
void CodeGen::gen_decl(Stmt* s) {
	VarDecl& d = s->decl;
	if (!d.is_array) {
		if (d.init) {
			// x = init;（含 char *p = "abc"：E_STR 生成地址）
			if (d.type.is_fp()) {
				const Symbol* sym = symtab_.lookup(d.name);
				if (!sym) { error("声明符号缺失: " + d.name); return; }
				gen_var_addr_to_b(*sym);
				if (d.type.is_double() || d.type.is_ldouble()) {
					gen_dp_expr(d.init);
					emit_dp_store_dp0_to_a_d1();
				} else {
					gen_fp_expr(d.init);
					emit_fp_store_fp0_to_a();
					if (tsize(d.type) == 8) emit_t("ZERO D1");
				}
				emit_store(d.type);
				return;
			}
			if (resolve_type(d.init).is_fp()) {
				gen_fp_expr(d.init);
				emit_t("F2I A, FP0");
				const Symbol* sym = symtab_.lookup(d.name);
				if (!sym) { error("声明符号缺失: " + d.name); return; }
				emit_t("PUSH DWORD A");
				gen_var_addr_to_b(*sym);
				emit_t("POP DWORD A");
				emit_store(d.type);
				return;
			}
			gen_expr(d.init);
			// _Bool 局部：任意标量值归一化为 0/1
			if (d.type.is_bool()) emit_bool_normalize();
			// long 局部：int 初始化器先提升为 long（unsigned 零扩展 / signed 符号扩展）
			if (tsize(d.type) == 8 && !resolve_type(d.init).is_long()) {
				if (resolve_type(d.init).is_unsigned_scalar()) emit_t("ZERO D1");
				else emit_d1_signext();
			}
			emit_t("PUSH DWORD A");
			const Symbol* sym = symtab_.lookup(d.name);
			if (!sym) { error("声明符号缺失: " + d.name); return; }
			gen_var_addr_to_b(*sym);
			emit_t("POP DWORD A");
			emit_store(sym->type);
		}
		return;
	}
	// 数组字符串初始化（char s[n] = "..."）
	if (d.has_str_init && type_is_char(d.type)) {
		std::string src = new_str_label();
		// 字符串常量入 data（追加到 data_）
		emit_str_data(src, d.str_init);
		const Symbol* sym = symtab_.lookup(d.name);
		if (!sym) { error("声明符号缺失: " + d.name); return; }
		// 拷贝 min(strlen+1, array_len) 字节
		size_t len = d.str_init.size() + 1;
		if (len > (size_t)d.array_len) len = d.array_len;
		emit_t("LET R, DWORD " + src);
		gen_var_addr_to_b(*sym);
		emit_t("LET C, DWORD " + std::to_string(len));
		std::string lp = new_label();
		emit_t(lp + ":");
		emit_t("LOD BYTE A");
		emit_t("ST BYTE *B, A");
		emit_t("INC B");
		emit_t("CDI");
		emit_t("LET E, DWORD " + lp);
		emit_t("JNZ");
	}
}

// ---------------- 表达式 ----------------

bool CodeGen::type_is_char(const Type& t) const {
	return !t.is_ptr() && t.base == B_CHAR;
}

int CodeGen::pointee_size(const Type& t) const {
	return tsize(t.pointee());
}

// 类型尺寸：struct/union 查布局；enum 按 int；其余按内置
int CodeGen::tsize(const Type& t) const {
	if (t.ptr_depth > 0) return 4;
	switch (t.base) {
		case B_STRUCT:
		case B_UNION: {
			if (!env_) return 0;
			const StructDef* d = env_->lookup_struct(t.tname);
			return d ? (int)d->size : 0;
		}
		case B_ENUM:
			return 4;
		case B_CHAR:
			return 1;
		case B_FLOAT:
			return 4;
		case B_DOUBLE:
			return 8;
		case B_LDOUBLE:
			return 8;
		case B_SHORT:
			return 2;
		case B_LONG:
			return 8;
		default:
			return 4;
	}
}

const MemberDef* CodeGen::member_def(const std::string& struct_name, const std::string& member) const {
	if (!env_) return nullptr;
	const StructDef* d = env_->lookup_struct(struct_name);
	if (!d) return nullptr;
	for (const MemberDef& m : d->members)
		if (m.name == member) return &m;
	return nullptr;
}

int CodeGen::member_offset(const std::string& struct_name, const std::string& member) const {
	const MemberDef* m = member_def(struct_name, member);
	return m ? (int)m->offset : 0;
}

// A *= sz。sz 为 2 的幂时用 SHL；否则用临时寄存器 R 做寄存器乘法
void CodeGen::emit_scale(int sz) {
	if (sz <= 1) return;
	if (sz == 2) { emit_t("SHL DWORD A, 1"); return; }
	if (sz == 4) { emit_t("SHL DWORD A, 2"); return; }
	if (sz == 8) { emit_t("SHL DWORD A, 3"); return; }
	// 非常规尺寸：LET R, sz; MUL DWORD A, R
	emit_t("LET R, DWORD " + std::to_string(sz));
	emit_t("MUL DWORD A, R");
	emit_t("MOV A, D2");
}

// 递归解析表达式类型：E_VAR 查符号表；E_BINOP 按操作数；E_UNOP 按规则
Type CodeGen::resolve_type(Expr* e) {
if (!e) return type_int();
switch (e->kind) {
case E_VAR: {
const Symbol* s = symtab_.lookup(e->name);
return s ? s->type : e->type;
}
case E_STR:
return type_str();
case E_INT:
// 超出 int32 范围的整数字面量按 long（64 位）处理
if (e->ival > INT32_MAX || e->ival < INT32_MIN) return type_long();
return type_int();
case E_FLOAT:
return e->type;
case E_BINOP: {
if (e->op == "&&" || e->op == "||") return type_int();
Type lt = resolve_type(e->l);
Type rt = resolve_type(e->r);
if ((lt.is_ptr() || rt.is_ptr()) && (e->op == "+" || e->op == "-"))
return lt.is_ptr() ? lt : rt;
// 比较 → int；移位 → 左操作数类型；算术/位运算任一 long → long
if (e->op == "==" || e->op == "!=" || e->op == "<" || e->op == "<=" ||
    e->op == ">" || e->op == ">=") return type_int();
if (e->op == "<<" || e->op == ">>") return lt;
if (lt.is_fp() || rt.is_fp()) return lt.is_fp() ? lt : rt;
if (lt.is_long() || rt.is_long())
return (lt.is_unsigned || rt.is_unsigned) ? type_ulong() : type_long();
return type_int();
}
case E_UNOP:
if (e->op == "&") return type_ptr(resolve_type(e->r));
if (e->op == "*") {
Type pt = resolve_type(e->r);
if (pt.is_func_ptr_type()) return pt;
return pt.pointee();
}
if (e->op == "-" || e->op == "~") {
Type t = resolve_type(e->r);
if (t.is_fp()) return t;
return t.is_long() ? t : type_int();
}
return type_int();// '!' 逻辑非 → int
case E_INDEX: {
Type bt = resolve_type(e->l);
return bt.is_ptr() ? bt.pointee() : bt;
}
case E_MEMBER:
return e->type;// 成员类型（parser 已解析）
case E_INCDEC:
return resolve_type(e->r);
case E_ASSIGN:
return resolve_type(e->l);
case E_COND:
return resolve_type(e->l);
case E_CALL: {
// 间接调用：通过函数指针表达式得到返回类型
if (e->l) {
Type ct = resolve_type(e->l);
if (ct.is_func_ptr_type()) return ct.func_ret_type();
}
// 直接函数调用（parser 的 e->type 是 int 占位，查函数表修正）
auto it = func_ret_.find("func_" + e->name);
return (it != func_ret_.end()) ? it->second : e->type;
}
case E_REGDIR:
return type_uint();// 寄存器是 unsigned int
case E_CAST:
return e->type;// 转换后的类型
default:
return e->type;
}
}

// LR 到 A（[A] 为地址）。signed char/short 读取后符号扩展（高位全 1）。
// long（8 字节）：A=低 32 位, D1=高 32 位。
void CodeGen::emit_load_from_a(Type t) {
	int sz = tsize(t);
	if (sz == 8) {
		emit_t("MOV R, A");					// R = 地址（A 即将被覆盖）
		emit_t("LR DWORD A, *R");			// A = 低 32 位
		emit_t("ADD DWORD R, 4");
		emit_t("LR DWORD D1, *R");			// D1 = 高 32 位
		return;
	}
	switch (sz) {
		case 1:
			emit_t("LR BYTE A, *A");
			if (!t.is_unsigned) {
				emit_t("SHL DWORD A, 24");
				emit_t("MSR DWORD A, 24");	// 符号扩展低 8 位
			}
			break;
		case 2:
			emit_t("LR WORD A, *A");
			if (!t.is_unsigned) {
				emit_t("SHL DWORD A, 16");
				emit_t("MSR DWORD A, 16");	// 符号扩展低 16 位
			}
			break;
		default:
			emit_t("LR DWORD A, *A");
			break;
	}
}

// ST 从 A（+D1 高字，long）到 [B]，按类型尺寸
void CodeGen::emit_store_to_b(Type t) {
	int sz = tsize(t);
	if (sz == 8) {
		emit_t("ST DWORD *B, A");			// 低 32 位
		emit_t("MOV R, B");
		emit_t("ADD DWORD R, 4");
		emit_t("ST DWORD *R, D1");			// 高 32 位
		return;
	}
	emit_t(sz == 1 ? "ST BYTE *B, A" : (sz == 2 ? "ST WORD *B, A" : "ST DWORD *B, A"));
}

void CodeGen::emit_store(Type t) {
	emit_store_to_b(t);
}

// 有符号除法/取余：A / B（均为 signed），结果 D2=商、D1=余数。
// 内联序列：记录符号 → 取绝对值 → 无符号 DIV → 按符号修正。
// 寄存器：T=恒 0 基准, R=商符号；被除数符号压栈保存（余修正用）。
// 注意：不使用 X/I（用户 __reg_ 可访问），避免干扰寄存器直访。
void CodeGen::emit_binop_signed_div() {
	// 0. T 恒 0（比较基准）
	emit_t("ZERO T");
	// 1. 被除数符号 → R，并压栈保存（余修正用）
	std::string la1 = new_label(), la2 = new_label();
	emit_t("MOV C, A");
	emit_t("CMP DWORD T");
	emit_t("LET E, DWORD " + la1);
	emit_t("JNL DWORD T");
	emit_t("LET R, DWORD 1");
	emit_t("LET E, DWORD " + la2);
	emit_t("JMP");
	emit_t(la1 + ":");
	emit_t("ZERO R");
	emit_t(la2 + ":");
	emit_t("PUSH DWORD R");			// [被除数符号]
	// 2. 商符号 = R ^ (B<0)：B<0 时 R 翻转（0/1 → 1-R）
	std::string lb1 = new_label();
	emit_t("MOV C, B");
	emit_t("CMP DWORD T");
	emit_t("LET E, DWORD " + lb1);
	emit_t("JNL DWORD T");			// B>=0：商符号 = R，跳过
	emit_t("MOV A, R");
	emit_t("MNE DWORD A");			// A = -R
	emit_t("ADD DWORD A, 1");		// A = 1-R（0↔1 翻转）
	emit_t("MOV R, A");
	emit_t(lb1 + ":");
	// 3. |A|
	std::string lav = new_label();
	emit_t("MOV C, A");
	emit_t("CMP DWORD T");
	emit_t("LET E, DWORD " + lav);
	emit_t("JNL DWORD T");
	emit_t("MNE DWORD A");
	emit_t(lav + ":");
	// 4. |B|
	std::string lbv = new_label();
	emit_t("MOV C, B");
	emit_t("CMP DWORD T");
	emit_t("LET E, DWORD " + lbv);
	emit_t("JNL DWORD T");
	emit_t("MNE DWORD B");
	emit_t(lbv + ":");
	// 5. 无符号除（除数为 0 → #DIV）
	emit_t("DIV DWORD A, B");		// D2=商, D1=余
	// 6. 商修正：if (R) D2 = -D2
	std::string lq = new_label();
	emit_t("MOV C, R");
	emit_t("CMP DWORD T");
	emit_t("LET E, DWORD " + lq);
	emit_t("JZ");
	emit_t("MNE DWORD D2");
	emit_t(lq + ":");
	// 7. 余修正：if (被除数符号) D1 = -D1（余数符号同被除数）
	std::string lr = new_label();
	emit_t("POP DWORD C");			// C = 被除数符号
	emit_t("CMP DWORD T");
	emit_t("LET E, DWORD " + lr);
	emit_t("JZ");
	emit_t("MNE DWORD D1");
	emit_t(lr + ":");
}

void CodeGen::gen_var_addr_to_a(const Symbol& s) {
	if (s.is_global) {
		emit_t("LET A, DWORD " + s.label);
	} else if (s.is_arg) {
		emit_t("MOV A, F");
		if (s.offset != 0) emit_t("SUB DWORD A, " + std::to_string(s.offset));
	} else {
		emit_t("MOV A, F");
		if (s.offset != 0) emit_t("ADD DWORD A, " + std::to_string(s.offset));
	}
}

void CodeGen::gen_var_addr_to_b(const Symbol& s) {
	if (s.is_global) {
		emit_t("LET B, DWORD " + s.label);
	} else if (s.is_arg) {
		emit_t("MOV B, F");
		if (s.offset != 0) emit_t("SUB DWORD B, " + std::to_string(s.offset));
	} else {
		emit_t("MOV B, F");
		if (s.offset != 0) emit_t("ADD DWORD B, " + std::to_string(s.offset));
	}
}

void CodeGen::gen_lvalue_addr(Expr* e) {
	if (e->kind == E_REGDIR) {
		// 寄存器不是内存，无地址（gen_assign / E_INCDEC 已特判处理）
		error("寄存器无内存地址: __reg_" + e->name);
		emit_t("LET B, DWORD 0");
		return;
	}
	if (e->kind == E_VAR) {
		const Symbol* s = symtab_.lookup(e->name);
		if (!s) {
			// 函数名作为函数地址：&foo / (unsigned)foo 等
			if (funcs_.count("func_" + e->name)) {
				emit_t("LET B, DWORD func_" + e->name);
				return;
			}
			error("左值未定义: " + e->name); emit_t("LET B, DWORD 0"); return;
		}
		if (s->is_array) {	// 数组名：基址
			gen_var_addr_to_b(*s);
			return;
		}
		gen_var_addr_to_b(*s);
		return;
	}
	if (e->kind == E_UNOP && e->op == "*") {
		// *p 左值：地址 = p 的值
		gen_expr(e->r);
		emit_t("MOV B, A");
		return;
	}
	if (e->kind == E_INDEX) {
		// 下标：数组名[i] 或 指针[i]
		// 基址表达式（数组名或指针值）
		Expr* base = e->l;
		Type bt = resolve_type(base);
		if (base && base->kind == E_VAR) {
			const Symbol* s = symtab_.lookup(base->name);
			if (s && s->is_array) {
				// 数组名[i]：基址 + i*元素尺寸
				gen_expr(e->r);
				emit_scale(tsize(s->type));
				emit_t("PUSH DWORD A");
				gen_var_addr_to_b(*s);
				emit_t("POP DWORD C");
				emit_t("ADD DWORD B, C");
				return;
			}
		}
		// 指针[i]（含指针表达式基址）：基址值 + i*元素尺寸
		gen_expr(base);
		emit_t("PUSH DWORD A");			// [base]
		gen_expr(e->r);
		if (bt.is_ptr()) {
			int esz = pointee_size(bt);
			emit_scale(esz);
		} else {
			emit_t("SHL DWORD A, 2");	// 默认 int 元素
		}
		emit_t("POP DWORD B");			// B = base
		emit_t("ADD DWORD B, A");		// B = base + i*size
		return;
	}
	if (e->kind == E_MEMBER) {
		// 成员左值地址 = 基地址 + 成员偏移
		Type bt = resolve_type(e->l);
		if (e->arrow) {
			// p->field：基址 = p 的值（指针解引用）
			gen_expr(e->l);
			emit_t("MOV B, A");
			bt = bt.pointee();
		} else {
			// s.field：基址 = s 的地址（支持嵌套成员递归）
			gen_lvalue_addr(e->l);
		}
		int off = member_offset(bt.tname, e->member);
		if (off != 0) emit_t("ADD DWORD B, " + std::to_string(off));
		return;
	}
	error("不是左值");
	emit_t("LET B, DWORD 0");
}

void CodeGen::gen_expr(Expr* e) {
	if (!e) { emit_t("LET A, DWORD 0"); return; }
	switch (e->kind) {
		case E_INT:
			emit_t("LET A, DWORD " + std::to_string(e->ival));
			// 超出 int32 范围的字面量：同时给出高字（值按 64 位二补码）
			if (e->ival > INT32_MAX || e->ival < INT32_MIN) {
				emit_t("LET D1, DWORD " + std::to_string((int32_t)(e->ival >> 32)));
			}
			break;
		case E_FLOAT: {
			if (e->type.is_double() || e->type.is_ldouble()) {
				double d = e->fval;
				uint64_t bits;
				memcpy(&bits, &d, sizeof(bits));
				char buf[32];
				snprintf(buf, sizeof(buf), "0x%016llX", (unsigned long long)bits);
				emit_t("DLDI DP0, " + std::string(buf));
			} else {
				float f = (float)e->fval;
				uint32_t bits;
				memcpy(&bits, &f, sizeof(bits));
				char buf[16];
				snprintf(buf, sizeof(buf), "0x%08X", bits);
				emit_t("FLDI FP0, " + std::string(buf));
			}
			break;
		}
		case E_STR: {
			// 字符串字面量 → 数据区常量地址（char*）
			std::string src = new_str_label();
			emit_str_data(src, e->name);
			emit_t("LET A, DWORD " + src);
			break;
		}
		case E_VAR: {
			const Symbol* s = symtab_.lookup(e->name);
			if (!s) {
				// 函数名作为函数地址（如 unsigned int a = foo;）
				if (funcs_.count("func_" + e->name)) {
					emit_t("LET A, DWORD func_" + e->name);
					break;
				}
				error("未定义变量: " + e->name); emit_t("LET A, DWORD 0"); break;
			}
			if (s->is_array) {	// 数组名 → 基址
				gen_var_addr_to_a(*s);
				break;
			}
			if (s->type.is_ptr()) {	// 指针变量：取指针值
				gen_var_addr_to_a(*s);
				emit_t("LR DWORD A, *A");
				break;
			}
			if (s->type.is_aggregate()) {	// 结构体变量右值 → 基址
				gen_var_addr_to_a(*s);
				break;
			}
			if (s->type.is_fp()) {
				gen_var_addr_to_a(*s);
				if (s->type.is_double() || s->type.is_ldouble()) {
					emit_t("DLD DP0, *A");
				} else {
					emit_t("LR DWORD A, *A");
					emit_fp_load_a_to_fp0();
				}
				break;
			}
			gen_var_addr_to_a(*s);
			emit_load_from_a(s->type);
			break;
		}
		case E_REGDIR:
			// __reg_X 右值：MOV A, X
			emit_t("MOV A, " + e->name);
			break;
		case E_CAST: {
			// 强制类型转换：求值后再截断/重解释/扩展
			gen_expr(e->r);
			Type src = e->r ? resolve_type(e->r) : type_int();
			if (e->type.is_fp() && !src.is_fp()) {
				// 整数 → 浮点
				if (e->type.is_double() || e->type.is_ldouble()) emit_t("I2D DP0, A");
				else emit_t("I2F FP0, A");
			} else if (!e->type.is_fp() && src.is_fp()) {
				// 浮点 → 整数
				if (src.is_double() || src.is_ldouble()) emit_t("D2I A, DP0");
				else emit_t("F2I A, FP0");
			} else if (e->type.is_fp() && src.is_fp()) {
				// 浮点 ↔ 浮点
				if ((e->type.is_double() || e->type.is_ldouble()) && src.is_float()) emit_t("F2D DP0, FP0");
				else if (e->type.is_float() && (src.is_double() || src.is_ldouble())) emit_t("D2F FP0, DP0");
			}
			int dsz = tsize(e->type);
			int ssz = tsize(src);
			// 转更小尺寸：截断低位（char 8 位 / short 16 位 / int 32 位）
			if (dsz < ssz) {
				int bits = 32 - dsz * 8;
				if (bits > 0) {
					emit_t("SHL DWORD A, " + std::to_string(bits));
					emit_t("SHR DWORD A, " + std::to_string(bits));
				}
			}
			// 扩大为 long（8 字节）：低位已就绪，D1 按目标符号性扩展
			else if (dsz == 8 && ssz < 8) {
				if (e->type.is_unsigned_scalar()) emit_t("ZERO D1");
				else emit_d1_signext();
			}
			// 其它转换（int↔unsigned、指针互转）数值不变，无操作
			break;
		}
		case E_ASSIGN:
			gen_assign(e);
			break;
		case E_BINOP:
			gen_binop(e);
			break;
		case E_UNOP: {
			if (e->op == "&") {
				// &x：左值地址 → A
				if (e->r && e->r->kind == E_REGDIR) {
					error("不能对寄存器取地址: __reg_" + e->r->name);
					emit_t("LET A, DWORD 0");
				} else if (e->r && (e->r->kind == E_VAR || e->r->kind == E_INDEX)) {
					gen_lvalue_addr(e->r);
					emit_t("MOV A, B");
				} else if (e->r && e->r->kind == E_UNOP && e->r->op == "*") {
					// &*p == p
					gen_expr(e->r->r);
				} else {
					error("& 操作数必须是左值");
					emit_t("LET A, DWORD 0");
				}
				break;
			}
			if (e->op == "*") {
				// *p：解引用读取。p 的值在 A → 作为地址 LR。
				// 函数指针的 * 仍是函数指示符，不读取数据内存，直接得到函数地址。
				if (!e->r) { error("* 缺操作数"); emit_t("LET A, DWORD 0"); break; }
				Type pt = resolve_type(e->r);
				if (!pt.is_ptr()) { error("* 操作数必须是指针"); emit_t("LET A, DWORD 0"); break; }
				gen_expr(e->r);
				if (!pt.is_func_ptr_type()) emit_load_from_a(pt.pointee());
				break;
			}
			if (e->op == "-") {
				gen_expr(e->r);
				if (resolve_type(e->r).is_long()) emit_long_neg();
				else emit_t("MNE DWORD A");
			}
			else if (e->op == "~") {
				gen_expr(e->r);
				if (resolve_type(e->r).is_long()) emit_long_not();
				else emit_t("NEG A");
			}
			else if (e->op == "!") {
				gen_cond(e->r);
				std::string lt = new_label(), ld = new_label();
				emit_t("MOV C, A");
				emit_t("ZERO T");
				emit_t("CMP DWORD T");
				emit_t("LET E, DWORD " + lt);
				emit_t("JZ");
				emit_t("LET A, DWORD 0");
				emit_t("LET E, DWORD " + ld);
				emit_t("JMP");
				emit_t(lt + ":");
				emit_t("LET A, DWORD 1");
				emit_t(ld + ":");
			}
			break;
		}
		case E_INCDEC: {
			// 寄存器 ++/--：__reg_X++ 直接读写寄存器（无内存地址）
			if (e->r && e->r->kind == E_REGDIR) {
				if (e->r->name == "E") {
					error("__reg_E 只能作为右值，不能自增/自减");
					emit_t("LET A, DWORD 0");
					break;
				}
				std::string reg = e->r->name;
				emit_t("MOV A, " + reg);			// A = 旧值
				if (e->postfix) emit_t("PUSH DWORD A");	// [old]
				emit_t((e->op == "++") ? "ADD DWORD A, 1" : "SUB DWORD A, 1");
				emit_t("MOV " + reg + ", A");		// 写回
				if (e->postfix) emit_t("POP DWORD A");	// 结果=旧值
				break;
			}
			// 左值地址 → B
			gen_lvalue_addr(e->r);
			emit_t("PUSH DWORD B");			// [addr]
			Type t = lvalue_type(e->r);
			if (tsize(t) == 8) {
				// long：64 位自增/自减
				emit_t("MOV A, B");
				emit_load_from_a(t);			// A:D1 = 旧值
				if (e->postfix) { emit_t("PUSH DWORD A"); emit_t("PUSH DWORD D1"); }	// [addr][old_lo][old_hi]
				if (e->op == "++") {
					emit_t("ADD DWORD A, 1");
					emit_t("MOV C, A");
					emit_t("ZERO T");
					emit_t("CMP DWORD T");
					std::string lnc = new_label();
					emit_t("LET E, DWORD " + lnc);
					emit_t("JNZ");
					emit_t("INC D1");			// 低字回绕 → 高字进位
					emit_t(lnc + ":");
				} else {
					emit_t("MOV C, A");
					emit_t("ZERO T");
					emit_t("CMP DWORD T");
					std::string lnb = new_label();
					emit_t("LET E, DWORD " + lnb);
					emit_t("JNZ");
					emit_t("DEC D1");			// 低字为 0 → 高字借位
					emit_t(lnb + ":");
					emit_t("SUB DWORD A, 1");
				}
				if (e->postfix) {
					// 栈：[addr][old_lo][old_hi]；A:D1 = 新值
					emit_t("POP DWORD D2");		// D2 = old_hi
					emit_t("POP DWORD C");		// C = old_lo
					emit_t("POP DWORD B");		// B = addr
					emit_store_to_b(t);			// 存新值
					emit_t("MOV A, C");
					emit_t("MOV D1, D2");		// 结果 = 旧值
				} else {
					emit_t("POP DWORD B");		// B = addr
					emit_store_to_b(t);			// 结果 = 新值（A:D1）
				}
				break;
			}
			// 指针 ++/--：步进 pointee_size；标量：+1
			int step = t.is_ptr() ? pointee_size(t) : 1;
			// 读取旧值（指针/标量都按 DWORD 读；char 低字节有效）
			emit_t("LR DWORD A, *B");
			if (e->postfix) emit_t("PUSH DWORD A");	// [addr][old]
			emit_t((e->op == "++") ? "ADD DWORD A, " + std::to_string(step)
			                       : "SUB DWORD A, " + std::to_string(step));
			if (e->postfix) {
				emit_t("POP DWORD B");		// B = old
				emit_t("POP DWORD C");		// C = addr
				emit_t(std::string("ST ") + type_size_word(t) + " *C, A");
				emit_t("MOV A, B");			// 结果 = old
			} else {
				emit_t("POP DWORD B");		// B = addr
				emit_t(std::string("ST ") + type_size_word(t) + " *B, A");
			}
			break;
		}
		case E_CALL:
			gen_call(e);
			break;
		case E_INDEX: {
			gen_lvalue_addr(e);
			emit_t("MOV A, B");
			if (lvalue_type(e).is_fp()) {
				if (lvalue_type(e).is_double() || lvalue_type(e).is_ldouble()) emit_t("DLD DP0, *A");
				else { emit_t("LR DWORD A, *A"); emit_fp_load_a_to_fp0(); }
			} else {
				emit_load_from_a(lvalue_type(e));
			}
			break;
		}
		case E_MEMBER: {
			// 标量成员读取：地址 → LR；结构体成员读取：得基址（左值）
			Type mt = lvalue_type(e);
			if (mt.is_aggregate()) {
				gen_lvalue_addr(e);
				emit_t("MOV A, B");	// 结构体成员左值 → 其地址
				break;
			}
			// 数组成员：作为数组名 → 基址（不读取指针值）
			{
				Type bt = resolve_type(e->l);
				if (e->arrow) bt = bt.pointee();
				const MemberDef* md = member_def(bt.tname, e->member);
				if (md && md->is_array) {
					gen_lvalue_addr(e);
					emit_t("MOV A, B");
					break;
				}
			}
			gen_lvalue_addr(e);
			emit_t("MOV A, B");
			if (mt.is_fp()) {
				if (mt.is_double() || mt.is_ldouble()) emit_t("DLD DP0, *A");
				else { emit_t("LR DWORD A, *A"); emit_fp_load_a_to_fp0(); }
			} else {
				emit_load_from_a(mt);
			}
			break;
		}
		case E_COND: {
			// c ? l : r
			std::string lf = new_label(), ld = new_label();
			gen_cond(e->c);
			emit_t("MOV C, A");
			emit_t("ZERO T");
			emit_t("CMP DWORD T");
			emit_t("LET E, DWORD " + lf);
			emit_t("JZ");
			gen_expr(e->l);
			emit_t("LET E, DWORD " + ld);
			emit_t("JMP");
			emit_t(lf + ":");
			gen_expr(e->r);
			emit_t(ld + ":");
			break;
		}
	}
}

void CodeGen::gen_assign(Expr* e) {
	// const 左值不可赋值
	if (lvalue_type(e->l).is_const) {
		error("不能给 const 变量赋值");
		emit_t("LET A, DWORD 0");
		return;
	}
	// 简单赋值
	if (e->op == "=") {
		if (e->l && e->l->kind == E_REGDIR) {
			if (e->l->name == "E") {
				error("__reg_E 只能作为右值，不能赋值");
				emit_t("LET A, DWORD 0");
				return;
			}
			// __reg_X = 值：值在 A → MOV X, A
			gen_expr(e->r);
			emit_t("MOV " + e->l->name + ", A");
			return;
		}
		Type lt = lvalue_type(e->l);
		if (lt.is_long()) {
			// long 标量赋值：int 右值先提升为 long
			gen_expr(e->r);
			if (!resolve_type(e->r).is_long()) {
				if (resolve_type(e->r).is_unsigned_scalar()) emit_t("ZERO D1");
				else emit_d1_signext();
			}
			emit_t("PUSH DWORD A");
			gen_lvalue_addr(e->l);
			emit_t("POP DWORD A");
			emit_store(lt);
			return;
		}
		if (lt.is_aggregate()) {
			// 结构体整体赋值：块拷贝 size 字节（源地址 → 目标地址）
			int sz = tsize(lt);
			gen_lvalue_addr(e->l);			// B = 目标
			emit_t("PUSH DWORD B");
			gen_expr(e->r);					// A = 源地址（结构体右值）
			emit_t("MOV R, A");
			emit_t("POP DWORD B");
			emit_t("LET C, DWORD " + std::to_string(sz));
			std::string lp = new_label();
			emit_t(lp + ":");
			emit_t("LOD BYTE A");
			emit_t("ST BYTE *B, A");
			emit_t("INC B");
			emit_t("CDI");
			emit_t("LET E, DWORD " + lp);
			emit_t("JNZ");
			return;
		}
		if (lt.is_fp()) {
			gen_lvalue_addr(e->l);
			if (lt.is_double() || lt.is_ldouble()) {
				gen_dp_expr(e->r);
				emit_dp_store_dp0_to_a_d1();
			} else {
				gen_fp_expr(e->r);
				emit_fp_store_fp0_to_a();
				if (tsize(lt) == 8) emit_t("ZERO D1");
			}
			emit_store(lt);
			return;
		}
		if (resolve_type(e->r).is_fp()) {
			// 浮点 → 整数赋值：转成 A 后按整数存储
			if (resolve_type(e->r).is_double() || resolve_type(e->r).is_ldouble()) {
				gen_dp_expr(e->r);
				emit_t("D2I A, DP0");
			} else {
				gen_fp_expr(e->r);
				emit_t("F2I A, FP0");
			}
			emit_t("PUSH DWORD A");
			gen_lvalue_addr(e->l);
			emit_t("POP DWORD A");
			emit_store(lt);
			return;
		}
		gen_expr(e->r);
		// _Bool 赋值：任意标量值归一化为 0/1
		if (lt.is_bool()) emit_bool_normalize();
		emit_t("PUSH DWORD A");
		gen_lvalue_addr(e->l);
		emit_t("POP DWORD A");
		emit_store(lvalue_type(e->l));
		return;
	}
	// 复合赋值 a op= b（寄存器目标特判：读旧值 → 运算 → 写回寄存器）
	if (e->l && e->l->kind == E_REGDIR) {
		if (e->l->name == "E") {
			error("__reg_E 只能作为右值，不能赋值");
			emit_t("LET A, DWORD 0");
			return;
		}
		std::string reg = e->l->name;
		emit_t("MOV A, " + reg);			// A = old
		emit_t("PUSH DWORD A");				// [old]
		gen_expr(e->r);						// A = 右
		emit_t("POP DWORD B");				// B = old
		std::string op = e->op.substr(0, e->op.size() - 1);	// 去掉 '='
		// A=右, B=old；结果统一到 A。寄存器是 unsigned int：/ % 用无符号 DIV，>> 用 SHR
		if (op == "+") { emit_t("ADD DWORD B, A"); emit_t("MOV A, B"); }
		else if (op == "-") { emit_t("SUB DWORD B, A"); emit_t("MOV A, B"); }
		else if (op == "*") { emit_t("MUL DWORD B, A"); emit_t("MOV A, D2"); }
		else if (op == "/") { emit_t("DIV DWORD B, A"); emit_t("MOV A, D2"); }
		else if (op == "%") { emit_t("DIV DWORD B, A"); emit_t("MOV A, D1"); }
		else if (op == "&") { emit_t("AND DWORD B, A"); emit_t("MOV A, B"); }
		else if (op == "|") { emit_t("OR DWORD B, A"); emit_t("MOV A, B"); }
		else if (op == "^") { emit_t("XOR DWORD B, A"); emit_t("MOV A, B"); }
		else if (op == "<<") {
			long long v; if (!is_const_int(e->r, v)) error("移位量须为常量");
			else { emit_t("SHL DWORD B, " + std::to_string(v)); emit_t("MOV A, B"); }
		}
		else if (op == ">>") {
			long long v; if (!is_const_int(e->r, v)) error("移位量须为常量");
			else { emit_t("SHR DWORD B, " + std::to_string(v)); emit_t("MOV A, B"); }
		}
		else error("不支持的复合赋值: " + e->op);
		emit_t("MOV " + reg + ", A");
		return;
	}
	// 复合赋值 a op= b
	gen_lvalue_addr(e->l);
	emit_t("PUSH DWORD B");				// [addr]
	Type t = lvalue_type(e->l);
	if (tsize(t) == 8) {
		// long：64 位读-改-写
		emit_t("MOV A, B");
		emit_load_from_a(t);			// A:D1 = old
		emit_t("PUSH DWORD A");
		emit_t("PUSH DWORD D1");		// [addr][old_lo][old_hi]
		gen_expr(e->r);					// A:D1 = 右
		if (!resolve_type(e->r).is_long()) {
			if (resolve_type(e->r).is_unsigned_scalar()) emit_t("ZERO D1");
			else emit_d1_signext();
		}
		emit_t("MOV B, A");				// B:R = 右
		emit_t("MOV R, D1");
		emit_t("POP DWORD D1");
		emit_t("POP DWORD A");			// A:D1 = old
		std::string lop = e->op.substr(0, e->op.size() - 1);
		bool luns = t.is_unsigned_scalar();
		if (lop == "+") emit_long_add();
		else if (lop == "-") emit_long_sub();
		else if (lop == "*") emit_long_mul();
		else if (lop == "/") emit_long_divmod(false, luns);
		else if (lop == "%") emit_long_divmod(true, luns);
		else if (lop == "&") { emit_t("AND DWORD A, B"); emit_t("MOV C, D1"); emit_t("AND DWORD C, R"); emit_t("MOV D1, C"); }
		else if (lop == "|") { emit_t("OR DWORD A, B"); emit_t("MOV C, D1"); emit_t("OR DWORD C, R"); emit_t("MOV D1, C"); }
		else if (lop == "^") { emit_t("XOR DWORD A, B"); emit_t("MOV C, D1"); emit_t("XOR DWORD C, R"); emit_t("MOV D1, C"); }
		else if (lop == "<<") {
			long long v; if (!is_const_int(e->r, v)) error("移位量须为常量");
			else emit_long_shift("<<", (int)v, luns);
		}
		else if (lop == ">>") {
			long long v; if (!is_const_int(e->r, v)) error("移位量须为常量");
			else emit_long_shift(">>", (int)v, luns);
		}
		else error("不支持的 long 复合赋值: " + e->op);
		emit_t("POP DWORD B");			// B = addr
		emit_store_to_b(t);
		return;
	}
	emit_t("LR DWORD A, *B");			// A = old（指针/标量统一 DWORD；char 低字节）
	emit_t("PUSH DWORD A");				// [addr][old]
	gen_expr(e->r);						// A = 右
	emit_t("POP DWORD B");				// B = old
	std::string op = e->op.substr(0, e->op.size() - 1);	// 去掉 '='
	if (op == "+") {
		// 指针 +=：右按元素尺寸缩放
		if (t.is_ptr()) {
			int sz = pointee_size(t);
			emit_scale(sz);
			emit_t("ADD DWORD B, A"); emit_t("MOV A, B");
		} else emit_t("ADD DWORD A, B");
	}
	else if (op == "-") {
		if (t.is_ptr()) {
			int sz = pointee_size(t);
			emit_scale(sz);
			emit_t("SUB DWORD B, A"); emit_t("MOV A, B");
		} else { emit_t("SUB DWORD B, A"); emit_t("MOV A, B"); }
	}
	else if (op == "*") { emit_t("MUL DWORD A, B"); emit_t("MOV A, D2"); }
	// 除法/取余非交换：POP B 后 A=右操作数, B=old；应 old/右
	else if (op == "/") {
		if (t.is_unsigned_scalar()) { emit_t("DIV DWORD B, A"); emit_t("MOV A, D2"); }
		else if (t.is_ptr()) { error("指针不支持 /= "); emit_t("LET A, DWORD 0"); }
		else {
			// signed：A=右, B=old → 交换为 A=old, B=右
			emit_t("MOV C, B"); emit_t("MOV B, A"); emit_t("MOV A, C");
			emit_binop_signed_div(); emit_t("MOV A, D2");
		}
	}
	else if (op == "%") {
		if (t.is_unsigned_scalar()) { emit_t("DIV DWORD B, A"); emit_t("MOV A, D1"); }
		else if (t.is_ptr()) { error("指针不支持 %= "); emit_t("LET A, DWORD 0"); }
		else {
			emit_t("MOV C, B"); emit_t("MOV B, A"); emit_t("MOV A, C");
			emit_binop_signed_div(); emit_t("MOV A, D1");
		}
	}
	else if (op == "&") emit_t("AND DWORD A, B");
	else if (op == "|") emit_t("OR DWORD A, B");
	else if (op == "^") emit_t("XOR DWORD A, B");
	else if (op == "<<") { long long v; if (!is_const_int(e->r, v)) error("移位量须为常量"); else emit_t("SHL DWORD A, " + std::to_string(v)); }
	else if (op == ">>") { long long v; if (!is_const_int(e->r, v)) error("移位量须为常量"); else emit_t((t.is_unsigned_scalar() ? "SHR DWORD A, " : "MSR DWORD A, ") + std::to_string(v)); }
	else error("不支持的复合赋值: " + e->op);
	emit_t("POP DWORD B");				// B = addr
	emit_store(t);
}

void CodeGen::gen_compare(Expr* e, const std::string& jump) {
	// 左在 A、右在 B（调用前已就绪），C = A - B
	(void)e;
	std::string lt = new_label(), ld = new_label();
	emit_t("ZERO T");
	emit_t("LET E, DWORD " + lt);
	emit_t(jump);
	emit_t("LET A, DWORD 0");
	emit_t("LET E, DWORD " + ld);
	emit_t("JMP");
	emit_t(lt + ":");
	emit_t("LET A, DWORD 1");
	emit_t(ld + ":");
}

void CodeGen::gen_shortcircuit(Expr* e, bool is_and) {
	// 短路语义：
	//   a && b：左假 → 0；右假 → 0；都真 → 1
	//   a || b：左真 → 1；右真 → 1；都假 → 0
	// lj = 短路提前跳转的“定论”标号：&& 是假路径(0)，|| 是真路径(1)。
	std::string lj = new_label(), ld = new_label();
	gen_cond(e->l);					// long 条件先归一化真值（低|高）
	emit_t("MOV C, A");
	emit_t("ZERO T");
	emit_t("CMP DWORD T");
	emit_t("LET E, DWORD " + lj);
	emit_t(is_and ? "JZ" : "JNZ");
	gen_cond(e->r);
	emit_t("MOV C, A");
	emit_t("CMP DWORD T");
	emit_t("LET E, DWORD " + lj);
	emit_t(is_and ? "JZ" : "JNZ");
	if (is_and) {
		// 两操作数都为真 → 结果 1
		emit_t("LET A, DWORD 1");
		emit_t("LET E, DWORD " + ld);
		emit_t("JMP");
		emit_t(lj + ":");
		emit_t("LET A, DWORD 0");
	} else {
		// 两操作数都为假 → 结果 0
		emit_t("LET A, DWORD 0");
		emit_t("LET E, DWORD " + ld);
		emit_t("JMP");
		emit_t(lj + ":");
		emit_t("LET A, DWORD 1");
	}
	emit_t(ld + ":");
}


static Expr* clone_expr_inline(const Expr* e,
                               const std::unordered_map<std::string, Expr*>& subst) {
if (!e) return nullptr;
Expr* n = new Expr;
*n = *e;// 先复制公共字段
n->l = n->r = n->c = nullptr;
n->args.clear();
if (e->kind == E_VAR) {
auto it = subst.find(e->name);
if (it != subst.end()) {
delete n;
return clone_expr_inline(it->second, subst);
}
}
if (e->l) n->l = clone_expr_inline(e->l, subst);
if (e->r) n->r = clone_expr_inline(e->r, subst);
if (e->c) n->c = clone_expr_inline(e->c, subst);
for (Expr* a : e->args) n->args.push_back(clone_expr_inline(a, subst));
return n;
}

bool CodeGen::function_inlinable(const Function* f) const {
if (!f || !f->is_inline || f->is_isr || f->is_vararg) return false;
if (f->body.size() != 1) return false;
Stmt* s = f->body[0];
if (!s || s->kind != S_RETURN || !s->expr) return false;
// 保守策略：内联体只允许一个 return 表达式，且不包含函数调用/循环/声明
// 这里通过检查 AST 中是否存在 E_CALL / S_WHILE / S_FOR / S_DECL 来拒绝复杂函数。
// 由于 body 已限制为单 return，简单检查 return 表达式中是否含 E_CALL。
std::function<bool(Expr*)> has_call = [&](Expr* x) -> bool {
if (!x) return false;
if (x->kind == E_CALL) return true;
if (x->l && has_call(x->l)) return true;
if (x->r && has_call(x->r)) return true;
if (x->c && has_call(x->c)) return true;
for (Expr* a : x->args) if (has_call(a)) return true;
return false;
};
return !has_call(s->expr);
}

void CodeGen::gen_inline_call(Expr* e, const Function* f) {
std::unordered_map<std::string, Expr*> subst;
for (size_t i = 0; i < f->params.size() && i < e->args.size(); i++) {
subst[f->param_names[i]] = e->args[i];
}
Expr* inlined = clone_expr_inline(f->body[0]->expr, subst);
gen_expr(inlined);
delete inlined;
}

void CodeGen::gen_call(Expr* e) {
// 调用约定（见 docs/calling-convention.md）：
//   调用方保存 F：PUSH F → 参数(左→右) → 返回地址 → JMP
//   返回后：清参数 → POP F 恢复。
//   long 参数占 8 字节（先压低字再压高字，内存小端序）；
//   long 返回值在 A(低):D1(高)，int 返回值经 D1 搬运到 A。
// 可变参数调用：PUSH F → 可变参数(左→右) → 命名参数(左→右) → 返回地址。
if (e->name == "__builtin_va_start") {
if (e->args.size() < 2) {
error("__builtin_va_start 需要两个参数");
return;
}
gen_lvalue_addr(e->args[0]);// B = &ap
emit_t("MOV A, F");
emit_t("SUB DWORD A, " + std::to_string(cur_func_named_bytes_ + 7));
emit_t("ST DWORD *B, A");
return;
}
if (e->name == "__builtin_va_arg") {
if (e->args.size() < 2) {
error("__builtin_va_arg 需要两个参数");
return;
}
long long sz = 4;
if (!is_const_int(e->args[1], sz)) {
error("__builtin_va_arg 的尺寸参数必须是编译期常量");
sz = 4;
}
gen_lvalue_addr(e->args[0]);// B = &ap
emit_t("PUSH DWORD B");// [&ap]
emit_t("LR DWORD A, *B");// A = ap
emit_t("PUSH DWORD A");// [&ap][old]
if (sz == 8) {
emit_t("LET A, DWORD 8");
} else {
emit_t("LET A, DWORD " + std::to_string(sz));
}
emit_t("POP DWORD B");// B = old
emit_t("PUSH DWORD B");// [&ap][old]（保存 old 指针）
emit_t("SUB DWORD B, A");// B = old - size
emit_t("MOV A, B");// A = new ap
emit_t("POP DWORD C");// C = old
emit_t("PUSH DWORD A");// [&ap][new]
emit_t("MOV A, C");// A = old
if (sz == 8) {
// long：低字在 old，高字在 old+4
emit_t("PUSH DWORD A");
emit_t("LR DWORD A, *A");
emit_t("PUSH DWORD A");// 暂存低字
emit_t("MOV A, C");
emit_t("ADD DWORD A, 4");
emit_t("LR DWORD A, *A");
emit_t("MOV D1, A");// 高字
emit_t("POP DWORD A");// 低字
} else {
emit_t("LR DWORD A, *A");
}
emit_t("POP DWORD C");// C = new ap
emit_t("POP DWORD B");// B = &ap
emit_t("ST DWORD *B, C");// ap = new
return;
}
Expr* callee = e->l;
std::string direct_name;
const Function* fn = nullptr;
Type fp_type;

// 直接函数调用：foo(...)。函数名不是变量，且存在于函数表。
if (callee && callee->kind == E_VAR && !symtab_.lookup(callee->name)) {
if (funcs_.count("func_" + callee->name)) {
direct_name = callee->name;
auto fit = funcs_.find("func_" + direct_name);
if (fit != funcs_.end()) fn = fit->second;
} else {
error("未定义函数: " + callee->name);
}
} else if (!callee && funcs_.count("func_" + e->name)) {
direct_name = e->name;
auto fit = funcs_.find("func_" + direct_name);
if (fit != funcs_.end()) fn = fit->second;
} else {
// 间接调用：函数指针表达式(...)。
if (callee) {
fp_type = resolve_type(callee);
if (!fp_type.is_func_ptr_type()) {
error("调用目标不是函数指针");
}
}
}

// inline 函数且满足内联条件：直接展开
if (!direct_name.empty() && fn && function_inlinable(fn)) {
gen_inline_call(e, fn);
return;
}

// 保存调用方帧指针（被调函数的 SFA 会覆盖 F，RER 不恢复）
emit_t("MOV A, F");
emit_t("PUSH DWORD A");
// 参数类型（直接调用查函数表；间接调用查函数指针类型；否则用实参类型）
int argbytes = 0;
auto push_arg = [&](Expr* a, Type pt, int& bytes) {
if (pt.is_fp()) {
if (pt.is_double() || pt.is_ldouble()) {
gen_dp_expr(a);
emit_dp_store_dp0_to_a_d1();
emit_t("PUSH DWORD A");// 低 32 位
emit_t("PUSH DWORD D1");// 高 32 位
bytes += 8;
} else {
gen_fp_expr(a);
emit_fp_store_fp0_to_a();
emit_t("PUSH DWORD A");
bytes += 4;
}
} else if (pt.is_long()) {
// long 参数：int 实参先提升为 long
if (resolve_type(a).is_fp()) {
gen_fp_expr(a);
emit_t("F2I A, FP0");
emit_d1_signext();
} else {
gen_expr(a);
if (!resolve_type(a).is_long()) {
if (resolve_type(a).is_unsigned_scalar()) emit_t("ZERO D1");
else emit_d1_signext();
}
}
emit_t("PUSH DWORD A");// 低字
emit_t("PUSH DWORD D1");// 高字（紧接其后，地址更高）
bytes += 8;
} else {
if (resolve_type(a).is_fp()) {
// 浮点实参传给整数参数：截断转整数
gen_fp_expr(a);
emit_t("F2I A, FP0");
} else {
gen_expr(a);
}
emit_t("PUSH DWORD A");
bytes += 4;
}
};
if (fn && fn->is_vararg) {
// 可变参数调用：先逆序压可变参数，再压命名参数。
// 这样第一个可变参数紧邻命名参数下方，地址 = F - (命名参数总槽位字节 + 7)，
// va_arg 每次读取后向低地址移动。
for (size_t j = e->args.size(); j-- > fn->params.size(); ) {
push_arg(e->args[j], resolve_type(e->args[j]), argbytes);
}
for (size_t i = 0; i < fn->params.size() && i < e->args.size(); i++) {
push_arg(e->args[i], fn->params[i], argbytes);
}
} else {
for (size_t i = 0; i < e->args.size(); i++) {
Expr* a = e->args[i];
Type pt;
if (fn && i < fn->params.size()) pt = fn->params[i];
else if (fp_type.is_func_ptr_type() && i < fp_type.func_params().size()) pt = fp_type.func_params()[i];
else pt = resolve_type(a);
push_arg(a, pt, argbytes);
}
}
// 返回地址
std::string ret = "RET" + std::to_string(label_cnt_++);
emit_t("LET E, DWORD " + ret);
emit_t("PUSH DWORD E");
// 跳转
if (!direct_name.empty()) {
std::string fn2 = "func_" + direct_name;
if (funcs_.find(fn2) == funcs_.end()) error("未定义函数: " + direct_name);
emit_t("LET E, DWORD " + fn2);
} else {
if (!callee) { error("缺少调用目标"); emit_t("LET E, DWORD 0"); }
else {
gen_expr(callee);
emit_t("MOV E, A");
}
}
emit_t("JMP");
emit_t(ret + ":");
// 清栈（参数总字节；F 的 4 字节由 POP 恢复）
emit_t("SUB DWORD S, " + std::to_string(argbytes));
// 恢复调用方帧指针
emit_t("POP DWORD F");
// 返回值：long 已在 A:D1；其它从 D1 取
bool ret_long = false;
bool ret_fp = false;
if (fn) { ret_long = fn->ret_type.is_long(); ret_fp = fn->ret_type.is_fp(); }
else if (fp_type.is_func_ptr_type()) { ret_long = fp_type.func_ret_type().is_long(); ret_fp = fp_type.func_ret_type().is_fp(); }
bool ret_double = fn ? (fn->ret_type.is_double() || fn->ret_type.is_ldouble()) : (fp_type.is_func_ptr_type() && (fp_type.func_ret_type().is_double() || fp_type.func_ret_type().is_ldouble()));
if (!ret_long && !ret_double) emit_t("MOV A, D1");
if (ret_fp) {
if (ret_double) emit_dp_load_a_d1_to_dp0();
else emit_fp_load_a_to_fp0();
}
}

void CodeGen::gen_cond(Expr* e) {
	// 生成 e 的逻辑真值（0/1）到 A。
	// long 条件：真值 = 低 32 位 | 高 32 位（任一字非零即真）。
	if (resolve_type(e).is_fp()) {
		if (resolve_type(e).is_double() || resolve_type(e).is_ldouble()) {
			gen_dp_expr(e);
			emit_t("DLDI DP1, 0");
			emit_t("DCMP DP0, DP1");
		} else {
			gen_fp_expr(e);
			emit_t("FLDI FP1, 0");
			emit_t("FCMP FP0, FP1");
		}
		std::string lf = new_label(), ld = new_label();
		emit_t("ZERO T");
		emit_t("CMP DWORD T");
		emit_t("LET E, DWORD " + lf);
		emit_t("JZ");
		emit_t("LET A, DWORD 1");
		emit_t("LET E, DWORD " + ld);
		emit_t("JMP");
		emit_t(lf + ":");
		emit_t("LET A, DWORD 0");
		emit_t(ld + ":");
		return;
	}
	gen_expr(e);
	if (resolve_type(e).is_long())
		emit_t("OR DWORD A, D1");
}

void CodeGen::gen_binop(Expr* e) {
	const std::string& op = e->op;
	Type lt = resolve_type(e->l);
	Type rt = resolve_type(e->r);

	// 短路逻辑
	if (op == "&&") { gen_shortcircuit(e, true); return; }
	if (op == "||") { gen_shortcircuit(e, false); return; }

	// 64 位 long 运算（任一操作数为 long 且非指针）
	if ((lt.base == B_LONG || rt.base == B_LONG) && !lt.is_ptr() && !rt.is_ptr()) {
		gen_binop_long(e);
		return;
	}

	// double 运算
	if ((lt.is_double() || lt.is_ldouble() || rt.is_double() || rt.is_ldouble()) && !lt.is_ptr() && !rt.is_ptr()) {
		gen_dp_expr(e->r);
		emit_t("DPUSH DP0");
		gen_dp_expr(e->l);
		emit_t("DPOP DP1");
		if (op == "==") { emit_t("DCMP DP0, DP1"); gen_compare(e, "JZ"); }
		else if (op == "!=") { emit_t("DCMP DP0, DP1"); gen_compare(e, "JNZ"); }
		else if (op == "<") { emit_t("DCMP DP0, DP1"); gen_compare(e, "JL DWORD T"); }
		else if (op == "<=") { emit_t("DCMP DP0, DP1"); gen_compare(e, "JNG DWORD T"); }
		else if (op == ">") { emit_t("DCMP DP0, DP1"); gen_compare(e, "JG DWORD T"); }
		else if (op == ">=") { emit_t("DCMP DP0, DP1"); gen_compare(e, "JNL DWORD T"); }
		else if (op == "+" || op == "-" || op == "*" || op == "/") emit_dp_binop(op);
		else error("不支持的 double 运算: " + op);
		return;
	}

	// 浮点运算
	if ((lt.is_fp() || rt.is_fp()) && !lt.is_ptr() && !rt.is_ptr()) {
		// 右操作数 → FP0，压栈；左操作数 → FP0；右操作数弹到 FP1
		gen_fp_expr(e->r);
		emit_t("FPUSH FP0");
		gen_fp_expr(e->l);
		emit_t("FPOP FP1");
		if (op == "==") { emit_t("FCMP FP0, FP1"); gen_compare(e, "JZ"); }
		else if (op == "!=") { emit_t("FCMP FP0, FP1"); gen_compare(e, "JNZ"); }
		else if (op == "<") { emit_t("FCMP FP0, FP1"); gen_compare(e, "JL DWORD T"); }
		else if (op == "<=") { emit_t("FCMP FP0, FP1"); gen_compare(e, "JNG DWORD T"); }
		else if (op == ">") { emit_t("FCMP FP0, FP1"); gen_compare(e, "JG DWORD T"); }
		else if (op == ">=") { emit_t("FCMP FP0, FP1"); gen_compare(e, "JNL DWORD T"); }
		else if (op == "+" || op == "-" || op == "*" || op == "/") emit_fp_binop(op);
		else error("不支持的浮点运算: " + op);
		return;
	}

	// 移位（右须为常量）；右移按左操作数符号选 SHR（无符号）/MSR（有符号）
	if (op == "<<" || op == ">>") {
		long long v;
		if (!is_const_int(e->r, v)) { error("移位量须为常量"); emit_t("LET A, DWORD 0"); return; }
		gen_expr(e->l);
		if (op == "<<") emit_t("SHL DWORD A, " + std::to_string(v));
		else emit_t((lt.is_unsigned_scalar() ? "SHR DWORD A, " : "MSR DWORD A, ") + std::to_string(v));
		return;
	}

	// 指针 ± 整数：缩放后加减
	if ((lt.is_ptr() || rt.is_ptr()) && (op == "+" || op == "-")) {
		// 求右压栈 → 求左 → POP B（B=右）
		gen_expr(e->r);
		emit_t("PUSH DWORD A");
		gen_expr(e->l);
		emit_t("POP DWORD B");			// B = 右
		if (lt.is_ptr()) {
			// 指针 ± 整数：A=指针, B=整数 → A ± B*sz
			int sz = pointee_size(lt);
			if (sz > 1) {
				// 保存指针 A → C；缩放 B（经 A）；恢复 A=指针
				emit_t("MOV C, A");
				emit_t("MOV A, B");
				emit_scale(sz);
				emit_t("MOV B, A");
				emit_t("MOV A, C");
			}
			if (op == "-") emit_t("SUB DWORD A, B");
			else emit_t("ADD DWORD A, B");
		} else {
			// 整数 + 指针：A=整数, B=指针 → 结果 A+B（右为指针）
			int sz = pointee_size(rt);
			emit_scale(sz);
			emit_t("ADD DWORD A, B");
		}
		return;
	}

	// 普通二元：求右压栈 → 求左 → POP B
	gen_expr(e->r);
	emit_t("PUSH DWORD A");
	gen_expr(e->l);
	emit_t("POP DWORD B");			// B = 右

	if (op == "+") { emit_t("ADD DWORD A, B"); return; }
	if (op == "-") { emit_t("SUB DWORD A, B"); return; }
	if (op == "*") { emit_t("MUL DWORD A, B"); emit_t("MOV A, D2"); return; }
	if (op == "/") {
		// 无符号除法：被除数（左）为 unsigned 时按位模式除（C 语义：int 转 unsigned）
		if (lt.is_unsigned_scalar()) { emit_t("DIV DWORD A, B"); emit_t("MOV A, D2"); }
		else { emit_binop_signed_div(); emit_t("MOV A, D2"); }
		return;
	}
	if (op == "%") {
		if (lt.is_unsigned_scalar()) { emit_t("DIV DWORD A, B"); emit_t("MOV A, D1"); }
		else { emit_binop_signed_div(); emit_t("MOV A, D1"); }
		return;
	}
	if (op == "&") { emit_t("AND DWORD A, B"); return; }
	if (op == "|") { emit_t("OR DWORD A, B"); return; }
	if (op == "^") { emit_t("XOR DWORD A, B"); return; }

	// 比较。符号性：两操作数均 unsigned → 无符号跳转。
	// 条件跳转直接比较 C 与操作数（无需 CMP）；== != 用 CMP 使 C=A-B 后 JZ/JNZ。
	bool uns = lt.is_unsigned_scalar() && rt.is_unsigned_scalar();
	if (op == "==" || op == "!=") {
		emit_t("MOV C, A");
		emit_t("CMP DWORD B");
		gen_compare(e, (op == "==") ? "JZ" : "JNZ");
	} else {
		emit_t("MOV C, A");
		if (op == "<") gen_compare(e, uns ? "JB DWORD B" : "JL DWORD B");
		else if (op == "<=") gen_compare(e, uns ? "JNB DWORD B" : "JNG DWORD B");
		else if (op == ">") gen_compare(e, uns ? "JA DWORD B" : "JG DWORD B");
		else if (op == ">=") gen_compare(e, uns ? "JNA DWORD B" : "JNL DWORD B");
		else error("不支持的运算符: " + op);
	}
}

// D1 = A 的符号扩展（int → long 提升）：A 的高位复制到 D1
void CodeGen::emit_d1_signext() {
	emit_t("MOV D1, A");
	emit_t("MSR DWORD D1, 31");	// 算术右移 31 → 全 1/全 0
}


void CodeGen::emit_bool_normalize() {
std::string lfalse = new_label(), lend = new_label();
emit_t("MOV C, A");
emit_t("ZERO T");
emit_t("CMP DWORD T");
emit_t("LET E, DWORD " + lfalse);
emit_t("JZ");
emit_t("LET A, DWORD 1");
emit_t("LET E, DWORD " + lend);
emit_t("JMP");
emit_t(lfalse + ":");
emit_t("LET A, DWORD 0");
emit_t(lend + ":");
}

void CodeGen::emit_fp_load_a_to_fp0() {
emit_t("PUSH DWORD A");
emit_t("FPOP FP0");
}

void CodeGen::emit_fp_store_fp0_to_a() {
emit_t("FPUSH FP0");
emit_t("POP DWORD A");
}

void CodeGen::emit_fp_binop(const std::string& op) {
if (op == "+") emit_t("FADD FP0, FP1");
else if (op == "-") emit_t("FSUB FP0, FP1");
else if (op == "*") emit_t("FMUL FP0, FP1");
else if (op == "/") emit_t("FDIV FP0, FP1");
else error("不支持的浮点运算: " + op);
}

void CodeGen::gen_fp_expr(Expr* e) {
if (!e) { emit_t("FLDI FP0, 0"); return; }
Type t = resolve_type(e);
if (t.is_fp()) {
gen_expr(e);
} else {
gen_expr(e);
emit_t("I2F FP0, A");
}
}


void CodeGen::emit_dp_load_a_d1_to_dp0() {
emit_t("PUSH DWORD A");
emit_t("PUSH DWORD D1");
emit_t("DPOP DP0");
}

void CodeGen::emit_dp_store_dp0_to_a_d1() {
emit_t("DPUSH DP0");
emit_t("POP DWORD D1");
emit_t("POP DWORD A");
}

void CodeGen::emit_dp_binop(const std::string& op) {
if (op == "+") emit_t("DADD DP0, DP1");
else if (op == "-") emit_t("DSUB DP0, DP1");
else if (op == "*") emit_t("DMUL DP0, DP1");
else if (op == "/") emit_t("DDIV DP0, DP1");
else error("不支持的 double 运算: " + op);
}

void CodeGen::gen_dp_expr(Expr* e) {
if (!e) { emit_t("DLDI DP0, 0"); return; }
Type t = resolve_type(e);
if (t.is_double() || t.is_ldouble()) {
gen_expr(e);
} else if (t.is_float()) {
gen_expr(e);// FP0
emit_t("F2D DP0, FP0");
} else {
gen_expr(e);// 整数在 A
emit_t("I2D DP0, A");
}
}

// ---- 64 位 long 运算辅助 ----
// 约定：A=低 32 位, D1=高 32 位；二元运算右操作数在 B(低)/R(高)。
// T 恒为 0（比较基准），E 为跳转目标（使用前必先 LET E）。

// A:D1 += B:R（低字相加，无符号进位传播到高字）。
// 进位 = 新低 < 右低（无符号，直接寄存器比较 JNB DWORD B）。
void CodeGen::emit_long_add() {
	std::string lno = new_label();
	emit_t("ADD DWORD A, B");			// A = 低和
	emit_t("MOV C, A");
	emit_t("LET E, DWORD " + lno);
	emit_t("JNB DWORD B");				// 新低 >= 右低 → 无进位
	emit_t("INC D1");					// 进位
	emit_t(lno + ":");
	emit_t("ADD DWORD D1, R");			// 高字相加
}

// A:D1 -= B:R。借位 = 旧低 < 右低（无符号，直接寄存器比较）。
void CodeGen::emit_long_sub() {
	std::string lno = new_label();
	emit_t("MOV C, A");					// C = 旧低
	emit_t("LET E, DWORD " + lno);
	emit_t("JNB DWORD B");				// 旧低 >= 右低 → 无借位
	emit_t("SUB DWORD D1, 1");			// 借位
	emit_t(lno + ":");
	emit_t("SUB DWORD A, B");
	emit_t("SUB DWORD D1, R");
}

// A:D1 = A:D1 * B:R（64×64 → 低 64 位）。
// 学校算法：t0 = xl*yl（64 位）、t1 = xl*yh、t2 = xh*yl；
// 结果低字 = t0 低字；结果高字 = t0 高字 + t1 低字 + t2 低字（mod 2^32）。
void CodeGen::emit_long_mul() {
	// 保存 xh（第一次 MUL 会覆盖 D1）
	emit_t("PUSH DWORD D1");			// [xh]
	// t0 = xl * yl → D1=t0高, D2=t0低（A、B 不被修改）
	emit_t("MUL DWORD A, B");
	emit_t("MOV C, D1");				// C = 累加器（t0 高字）
	emit_t("PUSH DWORD D2");			// [xh][t0低]（结果低字）
	// t1 = xl * yh → D2=t1低
	emit_t("MUL DWORD A, R");
	emit_t("ADD DWORD C, D2");
	// t2 = xh * yl → D2=t2低
	emit_t("POP DWORD D2");				// D2 = t0低
	emit_t("MOV A, D2");				// A = t0低（暂存）
	emit_t("POP DWORD D2");				// D2 = xh
	emit_t("PUSH DWORD A");				// [t0低]（压回）
	emit_t("MUL DWORD D2, B");			// D1:D2 = xh*yl → D2=t2低
	emit_t("ADD DWORD C, D2");
	// 结果：A=低, D1=高
	emit_t("POP DWORD A");
	emit_t("MOV D1, C");
}

// A:D1 = -(A:D1)：低字取负（-L = ~L+1）。
// 低字非 0（无进位）：高字取反 NEG（~H）；低字为 0（有进位）：高字取负 MNE（-H）。
void CodeGen::emit_long_neg() {
	std::string lnz = new_label(), ld = new_label();
	emit_t("MNE DWORD A");
	emit_t("MOV C, A");
	emit_t("ZERO T");
	emit_t("CMP DWORD T");
	emit_t("LET E, DWORD " + lnz);
	emit_t("JZ");					// 低字为 0 → 高字取负
	emit_t("NEG D1");				// 低字非 0 → 高字取反
	emit_t("LET E, DWORD " + ld);
	emit_t("JMP");
	emit_t(lnz + ":");
	emit_t("MNE DWORD D1");
	emit_t(ld + ":");
}

// A:D1 = ~(A:D1)
void CodeGen::emit_long_not() {
	emit_t("NEG A");
	emit_t("NEG D1");
}

// 无符号 64 位除法：A:D1 / B:R → 商 A:D1、余数 C:D2。
// 移位减法（恢复余数法），64 次迭代：
//   余数左移并入被除数最高位 → 被除数左移 → 若 余数≥除数：余数-=除数，商最低位置 1。
// 寄存器：X=余数低, I=余数高, D2=循环计数（进出时保存/恢复 X、I）。
void CodeGen::emit_long_udiv() {
	// 除数为 0（B==0 且 R==0）→ 触发硬件 #DIV
	emit_t("ZERO T");
	emit_t("MOV C, B");
	emit_t("CMP DWORD T");
	std::string lnz = new_label();
	emit_t("LET E, DWORD " + lnz);
	emit_t("JNZ");
	emit_t("MOV C, R");
	emit_t("CMP DWORD T");
	emit_t("LET E, DWORD " + lnz);
	emit_t("JNZ");
	emit_t("DIV DWORD A, B");			// B==0 → #DIV 异常
	emit_t(lnz + ":");
	// 保存用户 X/I
	emit_t("PUSH DWORD X");
	emit_t("PUSH DWORD I");
	// 状态初始化：X/I = 余数（0）；计数 64 压栈（D2 用作移位暂存；
	// E 是跳转专用寄存器，不能当普通暂存用——写 E 会触发代码区检查）
	emit_t("ZERO X");					// X = 余数低
	emit_t("ZERO I");					// I = 余数高
	emit_t("LET C, DWORD 64");
	emit_t("PUSH DWORD C");				// [cnt]
	emit_t("ZERO T");
	std::string lp = new_label();
	emit_t(lp + ":");
	// 1. carry_in = 被除数最高位（旧 D1 的 bit31）→ C
	emit_t("MOV C, D1");
	emit_t("SHR DWORD C, 31");
	// 2. 余数左移：D2=旧X的bit31；X=(X<<1)|carry_in；I=(I<<1)|D2
	emit_t("MOV D2, X");
	emit_t("SHR DWORD D2, 31");
	emit_t("SHL DWORD X, 1");
	emit_t("OR DWORD X, C");
	emit_t("SHL DWORD I, 1");
	emit_t("OR DWORD I, D2");
	// 3. 被除数左移：D2=旧A的bit31；A<<1；D1=(D1<<1)|D2
	emit_t("MOV D2, A");
	emit_t("SHR DWORD D2, 31");
	emit_t("SHL DWORD A, 1");
	emit_t("SHL DWORD D1, 1");
	emit_t("OR DWORD D1, D2");
	// 4. 若 余数(I:X) >= 除数(R:B)：余数 -= 除数；商最低位置 1
	//    先比较高字：I vs R（直接寄存器比较）
	emit_t("MOV C, I");
	std::string lsk = new_label(), lds = new_label();
	emit_t("LET E, DWORD " + lsk);
	emit_t("JB DWORD R");				// I < R → 跳过
	emit_t("LET E, DWORD " + lds);
	emit_t("JA DWORD R");				// I > R → 相减
	emit_t("MOV C, X");
	emit_t("LET E, DWORD " + lsk);
	emit_t("JB DWORD B");				// X < B → 跳过
	emit_t(lds + ":");
	// 余数 -= 除数（X-B，借位传 I-R）。借位 = 旧X < B（直接比较）
	emit_t("MOV C, X");
	emit_t("LET E, DWORD " + lsk + "_nb");
	emit_t("JNB DWORD B");
	emit_t("SUB DWORD I, 1");			// 借位
	emit_t(lsk + "_nb:");
	emit_t("SUB DWORD X, B");
	emit_t("SUB DWORD I, R");
	// 商最低位置 1（OR 不接受立即数操作数，用 C 中转）
	emit_t("LET C, DWORD 1");
	emit_t("OR DWORD A, C");
	emit_t(lsk + ":");
	// 5. 计数递减（计数器在栈顶）
	emit_t("POP DWORD D2");
	emit_t("DEC D2");
	emit_t("PUSH DWORD D2");
	emit_t("MOV C, D2");
	emit_t("ZERO T");
	emit_t("CMP DWORD T");
	emit_t("LET E, DWORD " + lp);
	emit_t("JNZ");
	// 弹出计数；余数 → C:D2
	emit_t("POP DWORD D2");
	emit_t("MOV C, X");
	emit_t("MOV D2, I");
	// 恢复用户 X/I
	emit_t("POP DWORD I");
	emit_t("POP DWORD X");
}

// 64 位除法/取余入口。uns=true 时两操作数均无符号。
// 有符号：记录符号 → 取绝对值 → 无符号核心 → 按符号修正商与余。
void CodeGen::emit_long_divmod(bool want_rem, bool uns) {
	if (uns) {
		emit_long_udiv();
		if (want_rem) { emit_t("MOV A, C"); emit_t("MOV D1, D2"); }
		return;
	}
	// s1 = 被除数符号（D1 的 bit31）
	emit_t("ZERO T");
	emit_t("MOV C, D1");
	emit_t("SHR DWORD C, 31");
	emit_t("PUSH DWORD C");				// [s1]
	emit_t("CMP DWORD T");
	std::string lp1 = new_label();
	emit_t("LET E, DWORD " + lp1);
	emit_t("JZ");
	emit_long_neg();					// |被除数|
	emit_t(lp1 + ":");
	// s2 = 除数符号（R 的 bit31）
	emit_t("ZERO T");
	emit_t("MOV C, R");
	emit_t("SHR DWORD C, 31");
	emit_t("PUSH DWORD C");				// [s1][s2]
	emit_t("CMP DWORD T");
	std::string lp2 = new_label();
	emit_t("LET E, DWORD " + lp2);
	emit_t("JZ");
	// |除数|：B:R → A:D1 → 取负 → B:R
	emit_t("MOV A, B");
	emit_t("MOV D1, R");
	emit_long_neg();
	emit_t("MOV B, A");
	emit_t("MOV R, D1");
	emit_t(lp2 + ":");
	// 无符号核心 → 商 A:D1、余 C:D2；此时除数 B:R 已无用，可作暂存
	emit_long_udiv();
	// 弹回 s2/s1 到 B/R（不碰余数 C:D2 与商 A:D1）
	emit_t("POP DWORD B");				// B = s2
	emit_t("POP DWORD R");				// R = s1
	// 保存 s1（余修正用）与余数（q_corr 计算会破坏 C/D2）
	emit_t("PUSH DWORD R");				// [s1]
	emit_t("PUSH DWORD C");				// [s1][rem_lo]
	emit_t("PUSH DWORD D2");			// [s1][rem_lo][rem_hi]
	// 商修正：q_corr = s1 ^ s2
	emit_t("XOR DWORD R, B");			// R = s1^s2
	emit_t("ZERO T");
	emit_t("MOV C, R");
	emit_t("CMP DWORD T");
	std::string lq = new_label();
	emit_t("LET E, DWORD " + lq);
	emit_t("JZ");
	emit_long_neg();					// 商取负
	emit_t(lq + ":");
	if (!want_rem) {
		// 结果 = 商（已在 A:D1）；丢弃余数与 s1
		emit_t("POP DWORD D2");			// rem_hi
		emit_t("POP DWORD C");			// rem_lo
		emit_t("POP DWORD C");			// s1
	} else {
		// 结果 = 余数：弹出并修正符号（余数符号同被除数 s1）
		emit_t("POP DWORD D2");			// rem_hi
		emit_t("POP DWORD C");			// rem_lo
		emit_t("MOV A, C");
		emit_t("MOV D1, D2");
		emit_t("POP DWORD C");			// C = s1
		emit_t("ZERO T");
		emit_t("CMP DWORD T");
		std::string lr = new_label();
		emit_t("LET E, DWORD " + lr);
		emit_t("JZ");
		emit_long_neg();				// 余取负
		emit_t(lr + ":");
	}
}

// A:D1 移位。op="<<" / ">>"；uns=true 右移为逻辑右移。v 为编译期常量。
void CodeGen::emit_long_shift(const std::string& op, int v, bool uns) {
	if (v == 0) return;
	if (v >= 64) {
		if (op == "<<" || uns) {
			emit_t("ZERO A");
			emit_t("ZERO D1");
		} else {
			// 有符号右移 ≥64：结果 = 全符号位（0 或 -1）
			emit_t("MOV A, D1");
			emit_t("MSR DWORD A, 31");
			emit_t("MOV D1, A");
		}
		return;
	}
	if (v >= 32) {
		if (op == "<<") {
			// (H*2^32+L) << v = (L << (v-32)) * 2^32 → 新低=0、新高=旧低<<(v-32)
			emit_t("MOV C, A");
			emit_t("SHL DWORD C, " + std::to_string(v - 32));
			emit_t("ZERO A");
			emit_t("MOV D1, C");
		} else {
			// 新低 = 旧高 >> (v-32)；新高 = 0（逻辑）/ 符号扩展（算术）
			emit_t("MOV A, D1");
			emit_t((uns ? "SHR DWORD A, " : "MSR DWORD A, ") + std::to_string(v - 32));
			if (uns) emit_t("ZERO D1");
			else emit_t("MSR DWORD D1, 31");
		}
		return;
	}
	// 0 < v < 32
	if (op == "<<") {
		// 新低 = A<<v；新高 = (D1<<v) | (旧A >> (32-v))
		emit_t("MOV C, A");
		emit_t("SHR DWORD C, " + std::to_string(32 - v));
		emit_t("SHL DWORD A, " + std::to_string(v));
		emit_t("SHL DWORD D1, " + std::to_string(v));
		emit_t("OR DWORD D1, C");
	} else {
		// 新低 = (A>>v) | (旧D1 << (32-v))；新高 = D1>>v（逻辑/算术）
		emit_t("MOV C, D1");
		emit_t("SHL DWORD C, " + std::to_string(32 - v));
		emit_t("SHR DWORD A, " + std::to_string(v));
		emit_t((uns ? "SHR DWORD D1, " : "MSR DWORD D1, ") + std::to_string(v));
		emit_t("OR DWORD A, C");
	}
}

// 64 位比较：(D1:A) vs (R:B)。比较后跳转 ltrue / lfalse（0/1 结果由调用方生成）。
// 高字定大小（按符号性），高字相等再比低字（恒无符号）。
void CodeGen::emit_long_cmp(const std::string& op, bool uns,
                            const std::string& ltrue, const std::string& lfalse) {
	// 比较语义：条件跳转直接比较 C 与操作数。
	// 相等性（==/!=）用 CMP 使 C=A-B 后 JZ/JNZ；关系比较（< <= > >=）
	// 直接 MOV C 后跳转（CMP 会破坏 C，不能混用）。
	if (op == "==" || op == "!=") {
		// 高字：D1 vs R
		emit_t("MOV C, D1");
		emit_t("CMP DWORD R");
		emit_t("LET E, DWORD " + ((op == "==") ? lfalse : ltrue));
		emit_t("JNZ");					// 高不等 → 假/真
		// 低字：A vs B
		emit_t("MOV C, A");
		emit_t("CMP DWORD B");
		emit_t("LET E, DWORD " + ((op == "==") ? lfalse : ltrue));
		emit_t("JNZ");
		return;
	}
	// 高字：D1 vs R（直接比较）。< <=：小于→真、大于→假；> >=：大于→真、小于→假
	emit_t("MOV C, D1");
	if (op == "<" || op == "<=") {
		emit_t("LET E, DWORD " + ltrue);
		emit_t((uns ? "JB DWORD R" : "JL DWORD R"));	// 高字小于 → 真
		emit_t("LET E, DWORD " + lfalse);
		emit_t((uns ? "JA DWORD R" : "JG DWORD R"));	// 高字大于 → 假
	} else {
		emit_t("LET E, DWORD " + ltrue);
		emit_t((uns ? "JA DWORD R" : "JG DWORD R"));	// 高字大于 → 真
		emit_t("LET E, DWORD " + lfalse);
		emit_t((uns ? "JB DWORD R" : "JL DWORD R"));	// 高字小于 → 假
	}
	// 低字：A vs B（高字相等时低字比较恒为无符号）
	emit_t("MOV C, A");
	if (op == "<")      { emit_t("LET E, DWORD " + ltrue); emit_t("JB DWORD B"); }
	else if (op == "<="){ emit_t("LET E, DWORD " + ltrue); emit_t("JNB DWORD B"); }
	else if (op == ">") { emit_t("LET E, DWORD " + ltrue); emit_t("JA DWORD B"); }
	else if (op == ">="){ emit_t("LET E, DWORD " + ltrue); emit_t("JNA DWORD B"); }
}

// 64 位 long 运算。值约定：A=低 32, D1=高 32。
// 求值后：A=左低, D1=左高, B=右低, R=右高。
void CodeGen::gen_binop_long(Expr* e) {
	const std::string& op = e->op;
	Type lt = resolve_type(e->l);
	Type rt = resolve_type(e->r);

	// 移位（右须为常量）：先做，避免对常量右操作数做无谓求值
	if (op == "<<" || op == ">>") {
		long long v;
		if (!is_const_int(e->r, v)) { error("移位量须为常量"); emit_t("LET A, DWORD 0"); emit_t("ZERO D1"); return; }
		// 求左（若左是 int 提升为 long）
		gen_expr(e->l);
		if (lt.base != B_LONG) {
			if (lt.is_unsigned_scalar()) emit_t("ZERO D1");
			else emit_d1_signext();
		}
		emit_long_shift(op, (int)v, lt.is_unsigned_scalar());
		return;
	}

	// 求右：若右是 int 提升为 long（unsigned 零扩展 / signed 符号扩展）
	gen_expr(e->r);
	if (rt.base != B_LONG) {
		if (rt.is_unsigned_scalar()) emit_t("ZERO D1");
		else emit_d1_signext();
	}
	// 右操作数（低/高字）压栈保存：左操作数求值可能使用 R（long 加载）
	emit_t("PUSH DWORD A");			// [右低]
	emit_t("PUSH DWORD D1");		// [右低][右高]
	// 求左：若左是 int 提升为 long
	gen_expr(e->l);
	if (lt.base != B_LONG) {
		if (lt.is_unsigned_scalar()) emit_t("ZERO D1");
		else emit_d1_signext();
	}
	emit_t("POP DWORD R");			// R = 右高
	emit_t("POP DWORD B");			// B = 右低

	if (op == "+") { emit_long_add(); return; }
	if (op == "-") { emit_long_sub(); return; }
	if (op == "*") { emit_long_mul(); return; }
	bool uns = lt.is_unsigned_scalar() && rt.is_unsigned_scalar();
	if (op == "/") { emit_long_divmod(false, uns); return; }
	if (op == "%") { emit_long_divmod(true, uns); return; }
	if (op == "&") {
		emit_t("AND DWORD A, B");
		emit_t("MOV C, D1");
		emit_t("AND DWORD C, R");
		emit_t("MOV D1, C");
		return;
	}
	if (op == "|") {
		emit_t("OR DWORD A, B");
		emit_t("MOV C, D1");
		emit_t("OR DWORD C, R");
		emit_t("MOV D1, C");
		return;
	}
	if (op == "^") {
		emit_t("XOR DWORD A, B");
		emit_t("MOV C, D1");
		emit_t("XOR DWORD C, R");
		emit_t("MOV D1, C");
		return;
	}

	// 比较：先比高字，相等再比低字；结果 0/1 到 A
	// emit_long_cmp 之后自然落空的是“条件不满足”路径（== 例外：落空=相等=真）
	if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
		std::string ltrue = new_label(), lfalse = new_label(), ldone = new_label();
		emit_long_cmp(op, uns, ltrue, lfalse);
		if (op == "==") {
			emit_t(ltrue + ":");
			emit_t("LET A, DWORD 1");
			emit_t("LET E, DWORD " + ldone);
			emit_t("JMP");
			emit_t(lfalse + ":");
			emit_t("LET A, DWORD 0");
		} else {
			emit_t(lfalse + ":");
			emit_t("LET A, DWORD 0");
			emit_t("LET E, DWORD " + ldone);
			emit_t("JMP");
			emit_t(ltrue + ":");
			emit_t("LET A, DWORD 1");
		}
		emit_t(ldone + ":");
		return;
	}

	error("不支持的 long 运算: " + op);
}

} // namespace dcc
