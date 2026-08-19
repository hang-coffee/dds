// main.cpp - dcc 命令行入口
//
// 用法: dcc <input.c> [output.asm]
//   默认输出为 <input 去掉扩展名>.asm，与输入同目录
//
// 流水线: 读文件 → 预处理(#include/#define) → Lexer → Parser → CodeGen →
//         写出 dasm 汇编文本
// 任一阶段出错即停止，错误打印到 stderr，退出码 1。

#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "preprocessor.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// 默认 include 目录（Makefile 编译时注入：项目根/tools/dcc/include）
#ifndef DCC_INCLUDE_DIR
#define DCC_INCLUDE_DIR ""
#endif

namespace {

// 读整个文件；失败返回 false
bool read_file(const std::string& path, std::string& out) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	std::ostringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}

bool file_exists(const std::string& path) {
	std::ifstream in(path);
	return (bool)in;
}

std::string join_path(const std::string& base, const std::string& name) {
	if (base.empty()) return name;
	if (base.back() == '/') return base + name;
	return base + "/" + name;
}

bool write_file(const std::string& path, const std::string& content) {
	std::ofstream out(path, std::ios::binary);
	if (!out) return false;
	out << content;
	out.close();
	return (bool)out;
}

void print_usage(const char* prog) {
	std::fprintf(stderr, "用法: %s <in1.c> [in2.c ...] [output.asm]\n", prog);
	std::fprintf(stderr, "  dcc: DOCTOR 的 C 子集编译器（int/char → dasm 汇编文本）\n");
	std::fprintf(stderr, "  多文件编译：声明在头文件（.h），实现在各 .c，合并输出一个 .asm\n");
	std::fprintf(stderr, "  最后一个参数以 .asm 结尾视为输出；否则默认 <第一个输入去扩展名>.asm\n");
}

std::string default_out_path(const std::string& in) {
	size_t dot = in.rfind('.');
	size_t slash = in.rfind('/');
	// 只去掉最后一个扩展名；无扩展名则原样加 .asm
	std::string base = (dot != std::string::npos && (slash == std::string::npos || dot > slash))
	                   ? in.substr(0, dot) : in;
	return base + ".asm";
}

} // namespace

// 合并多个编译单元的 Program：
//   - 全局变量：extern 声明去重（需有定义）；同名定义冲突报错；extern 无定义报错
//   - 函数：原型与定义去重；同名定义冲突报错
// 返回 false 表示有错误（errs 收集）
static bool merge_programs(std::vector<dcc::Program>& progs, dcc::Program& out,
                           std::vector<std::string>& errs) {
	std::unordered_map<std::string, int> defined_globals;	// name → out.globals 索引
	std::unordered_map<std::string, int> defined_funcs;		// name → out.funcs 索引
	std::vector<std::string> extern_globals;				// 仅 extern 声明（无定义者）

	for (dcc::Program& p : progs) {
		for (dcc::GlobalVar& g : p.globals) {
			if (g.is_extern) {
				if (!defined_globals.count(g.name)) extern_globals.push_back(g.name);
				continue;
			}
			if (defined_globals.count(g.name)) {
				errs.push_back("全局变量重复定义: " + g.name);
				return false;
			}
			defined_globals[g.name] = (int)out.globals.size();
			out.globals.push_back(std::move(g));
		}
		for (dcc::Function& f : p.funcs) {
			if (f.is_decl) {
				// 原型：若尚无定义则保留（供函数表登记）；已有定义则忽略
				if (!defined_funcs.count(f.name)) {
					defined_funcs[f.name] = (int)out.funcs.size();
					out.funcs.push_back(std::move(f));
				}
				continue;
			}
			// 定义：若已有同名 → 若之前是原型则替换为定义；若是定义则冲突
			auto it = defined_funcs.find(f.name);
			if (it != defined_funcs.end()) {
				if (out.funcs[it->second].is_decl) {
					// 原型 → 定义：继承原型的 __interrupt__ 标记
					bool isr = out.funcs[it->second].is_isr;
					out.funcs[it->second] = std::move(f);
					out.funcs[it->second].is_isr = isr;
					continue;
				}
				errs.push_back("函数重复定义: " + f.name);
				return false;
			}
			defined_funcs[f.name] = (int)out.funcs.size();
			out.funcs.push_back(std::move(f));
		}
	}
	// extern 变量必须有定义
	for (const std::string& n : extern_globals) {
		if (!defined_globals.count(n)) {
			errs.push_back("extern 变量未定义: " + n);
			return false;
		}
	}
	return true;
}

int main(int argc, char** argv) {
	// 参数解析：
	//   dcc <in.c> [more.c ...] [out.asm]
	// 最后一个参数以 .asm 结尾视为输出文件；否则默认 <第一个输入去扩展名>.asm
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}
	std::vector<std::string> inputs;
	std::string out_path;
	{
		std::string last = argv[argc - 1];
		bool last_is_asm = (last.size() >= 4 && last.compare(last.size() - 4, 4, ".asm") == 0);
		if (last_is_asm && argc >= 3) {
			out_path = last;
			for (int i = 1; i < argc - 1; i++) inputs.push_back(argv[i]);
		} else {
			for (int i = 1; i < argc; i++) inputs.push_back(argv[i]);
			out_path = default_out_path(inputs[0]);
		}
	}
	if (inputs.empty()) {
		print_usage(argv[0]);
		return 1;
	}

	std::string include_dir = DCC_INCLUDE_DIR;
	if (const char* env = std::getenv("DCC_INCLUDE")) include_dir = env;	// 环境变量覆盖
	// lib 目录：与 include 目录同级（项目根/tools/dcc/lib）
	std::string lib_dir = include_dir;
	{
		size_t p = lib_dir.rfind('/');
		if (p != std::string::npos) lib_dir = lib_dir.substr(0, p);
		else lib_dir.clear();
		if (!lib_dir.empty()) lib_dir += "/lib";
	}

	std::vector<dcc::Program> progs;
	std::vector<std::string> all_includes;
	std::set<std::string> processed;	// 已处理文件的 basename（防重复/防循环）
	dcc::TypeEnv tenv;					// struct/union/enum/typedef 环境（跨编译单元共享）

	// 编译单个 .c：读 → 预处理 → 词法 → 语法 → progs
	auto compile_one = [&](const std::string& path) -> bool {
		// 取 basename（去目录去扩展名）用于去重
		std::string base = path;
		{
			size_t s = base.rfind('/');
			if (s != std::string::npos) base = base.substr(s + 1);
			size_t dot = base.rfind('.');
			if (dot != std::string::npos) base = base.substr(0, dot);
		}
		if (processed.count(base)) return true;		// 已处理（含显式输入与 lib 去重）
		processed.insert(base);

		std::string src;
		if (!read_file(path, src)) {
			std::fprintf(stderr, "dcc: 无法打开输入文件 '%s'\n", path.c_str());
			return false;
		}
		dcc::PreprocessResult pr = dcc::preprocess(src, path, include_dir);
		if (!pr.ok()) {
			for (const std::string& e : pr.errs)
				std::fprintf(stderr, "dcc: %s\n", e.c_str());
			std::fprintf(stderr, "dcc: 预处理失败: %s\n", path.c_str());
			return false;
		}
		all_includes.insert(all_includes.end(), pr.includes.begin(), pr.includes.end());
		dcc::Lexer lexer(pr.text);
		std::vector<dcc::Token> toks = lexer.tokenize();
		if (!lexer.errors().empty()) {
			for (const std::string& e : lexer.errors())
				std::fprintf(stderr, "dcc: %s\n", e.c_str());
			std::fprintf(stderr, "dcc: 词法分析失败: %s\n", path.c_str());
			return false;
		}
		dcc::Parser parser(toks, tenv);
		dcc::Program prog = parser.parse();
		if (!parser.errors().empty()) {
			for (const std::string& e : parser.errors())
				std::fprintf(stderr, "dcc: %s\n", e.c_str());
			std::fprintf(stderr, "dcc: 语法分析失败: %s\n", path.c_str());
			return false;
		}
		progs.push_back(std::move(prog));
		return true;
	};

	// 1. 编译显式输入
	for (const std::string& in_path : inputs) {
		if (!compile_one(in_path)) return 1;
	}

	// 2. 自动查找 lib：对每个被 include 的头文件，若 lib/<name>.c 存在则编译合并
	//    （lib 文件自身 include 的头也递归查找；processed 防循环/防重复）
	for (size_t qi = 0; qi < all_includes.size(); qi++) {
		const std::string& hdr = all_includes[qi];
		std::string lib_path = join_path(lib_dir, hdr + ".c");
		if (processed.count(hdr)) continue;			// 已处理（用户显式实现或已加载）
		if (file_exists(lib_path)) {
			if (!compile_one(lib_path)) return 1;
		}
	}

	// 合并多文件（extern/原型去重）
	dcc::Program prog;
	std::vector<std::string> merr;
	if (!merge_programs(progs, prog, merr)) {
		for (const std::string& e : merr)
			std::fprintf(stderr, "dcc: %s\n", e.c_str());
		std::fprintf(stderr, "dcc: 合并失败\n");
		return 1;
	}

	// 代码生成
	dcc::CodeGen cg;
	std::string asm_text = cg.generate(prog, tenv);
	if (!cg.errors().empty()) {
		for (const std::string& e : cg.errors())
			std::fprintf(stderr, "dcc: %s\n", e.c_str());
		std::fprintf(stderr, "dcc: 代码生成失败\n");
		return 1;
	}

	// 写出
	if (!write_file(out_path, asm_text)) {
		std::fprintf(stderr, "dcc: 无法写入输出文件 '%s'\n", out_path.c_str());
		return 1;
	}

	std::fprintf(stderr, "dcc: %zu 个输入文件 → %s (ok)\n", inputs.size(), out_path.c_str());
	return 0;
}
