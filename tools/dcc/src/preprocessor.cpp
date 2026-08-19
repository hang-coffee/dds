// preprocessor.cpp - dcc 预处理器实现
//
// 支持的指令：
//   #include <foo.h>   /   #include "foo.h"
//   #define NAME body
//   #define NAME(p1, p2) body      （函数宏）
//   #undef NAME
//   #ifdef NAME  /  #ifndef NAME  /  #else  /  #endif
//   #pragma ...（忽略）
//
// 其它 # 指令报错。宏展开为文本级 token 替换：对象宏与函数宏，
// 实参先展开再替换；宏体递归展开（防自递归）；字符串/字符字面量内不展开。
//
// 限制：
//   - 不支持 #if/#elif 表达式、#error、可变参数宏、续行符 '\' 跨行宏
//   - 被包含文件内同样预处理；宏表跨文件共享，条件栈每文件独立

#include "preprocessor.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>

namespace dcc {

namespace {

struct Macro {
	bool is_func;
	std::vector<std::string> params;
	std::string body;
};

struct CondFrame {
	bool parent_active;		// 进入本层前的激活状态
	bool taken;				// 本层当前分支是否激活（含父层）
	bool has_else;
};

struct PreCtx {
	std::string include_dir;
	std::unordered_map<std::string, Macro> macros;
	std::vector<std::string> errs;
	std::set<std::string> includes;	// 被 include 的头文件 basename（去扩展名，去重）
	int depth = 0;			// include 嵌套深度
};

// 前向声明（handle_directive 递归 include 需要）
void process_file_impl(PreCtx& ctx, const std::string& text,
                       const std::string& fname, std::string& out);

// ---------- 工具 ----------

static std::string trim(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) a++;
	while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
	return s.substr(a, b - a);
}

static bool is_ident_start(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
static bool is_ident_part(char c)  { return std::isalnum((unsigned char)c) || c == '_'; }

static std::string join_path(const std::string& base, const std::string& name) {
	if (base.empty()) return name;
	if (base.back() == '/') return base + name;
	return base + "/" + name;
}

static std::string dir_name(const std::string& f) {
	size_t p = f.rfind('/');
	if (p == std::string::npos) return ".";
	return f.substr(0, p);
}

static bool file_exists(const std::string& path) {
	std::ifstream in(path);
	return (bool)in;
}

static bool read_file(const std::string& path, std::string& out) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	std::ostringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}

// 剥离注释（// 与 /* */），保留换行；字符串/字符字面量内不处理
static std::string strip_comments(const std::string& in) {
	std::string out;
	size_t i = 0, n = in.size();
	while (i < n) {
		char c = in[i];
		if (c == '"' || c == '\'') {
			char q = c;
			out += c; i++;
			while (i < n) {
				out += in[i];
				if (in[i] == '\\' && i + 1 < n) { out += in[i + 1]; i += 2; continue; }
				if (in[i] == q) { i++; break; }
				i++;
			}
			continue;
		}
		if (c == '/' && i + 1 < n && in[i + 1] == '/') {
			while (i < n && in[i] != '\n') { out += ' '; i++; }
			continue;
		}
		if (c == '/' && i + 1 < n && in[i + 1] == '*') {
			i += 2;
			while (i + 1 < n && !(in[i] == '*' && in[i + 1] == '/')) {
				if (in[i] == '\n') out += '\n'; else out += ' ';
				i++;
			}
			if (i + 1 < n) i += 2; else i = n;
			continue;
		}
		out += c; i++;
	}
	return out;
}

// 递归展开一行文本中的宏；skip 为"正在展开的宏名"（防自递归）
std::string expand_line(PreCtx& ctx, const std::string& line, std::set<std::string>& skip);

// 解析函数宏调用实参：s[pos] == '('；结束后 pos 指向 ')' 之后。
// 实参先各自展开（跳过 skip 中的宏）。
static bool parse_call_args(PreCtx& ctx, const std::string& s, size_t& pos,
                            std::vector<std::string>& args, std::set<std::string>& skip) {
	size_t i = pos + 1, n = s.size();
	int depth = 1;
	std::string cur;
	while (i < n) {
		char c = s[i];
		if (c == '"' || c == '\'') {
			char q = c;
			cur += c; i++;
			while (i < n) {
				cur += s[i];
				if (s[i] == '\\' && i + 1 < n) { cur += s[i + 1]; i += 2; continue; }
				if (s[i] == q) { i++; break; }
				i++;
			}
			continue;
		}
		if (c == '(') { depth++; cur += c; i++; continue; }
		if (c == ')') {
			depth--;
			if (depth == 0) {
				args.push_back(expand_line(ctx, cur, skip));
				pos = i + 1;
				return true;
			}
			cur += c; i++; continue;
		}
		if (c == ',' && depth == 1) {
			args.push_back(expand_line(ctx, cur, skip));
			cur.clear(); i++; continue;
		}
		cur += c; i++;
	}
	return false;	// 未闭合
}

// 宏体中的参数名替换为实参文本
static std::string substitute_params(const std::string& body,
                                     const std::vector<std::string>& params,
                                     const std::vector<std::string>& args) {
	std::string out;
	size_t i = 0, n = body.size();
	while (i < n) {
		char c = body[i];
		if (is_ident_start(c)) {
			size_t j = i;
			while (j < n && is_ident_part(body[j])) j++;
			std::string word = body.substr(i, j - i);
			bool found = false;
			for (size_t k = 0; k < params.size(); k++) {
				if (params[k] == word) { out += args[k]; found = true; break; }
			}
			if (!found) out += word;
			i = j;
			continue;
		}
		out += c; i++;
	}
	return out;
}

std::string expand_line(PreCtx& ctx, const std::string& line, std::set<std::string>& skip) {
	std::string out;
	size_t i = 0, n = line.size();
	while (i < n) {
		char c = line[i];
		if (c == '"' || c == '\'') {
			char q = c;
			out += c; i++;
			while (i < n) {
				out += line[i];
				if (line[i] == '\\' && i + 1 < n) { out += line[i + 1]; i += 2; continue; }
				if (line[i] == q) { i++; break; }
				i++;
			}
			continue;
		}
		if (is_ident_start(c)) {
			size_t j = i;
			while (j < n && is_ident_part(line[j])) j++;
			std::string word = line.substr(i, j - i);
			i = j;
			auto it = ctx.macros.find(word);
			if (it == ctx.macros.end() || skip.count(word)) { out += word; continue; }
			Macro& m = it->second;
			if (!m.is_func) {
				skip.insert(word);
				out += expand_line(ctx, m.body, skip);
				skip.erase(word);
				continue;
			}
			// 函数宏：跳过空白看 '('
			size_t k = i;
			while (k < n && std::isspace((unsigned char)line[k])) k++;
			if (k < n && line[k] == '(') {
				std::vector<std::string> args;
				if (!parse_call_args(ctx, line, k, args, skip)) {
					ctx.errs.push_back("宏调用括号未闭合: " + word);
					out += word;
					continue;
				}
				if (args.size() != m.params.size()) {
					ctx.errs.push_back("宏 " + word + " 参数个数不匹配（期望 " +
					                   std::to_string(m.params.size()) + "，实际 " +
					                   std::to_string(args.size()) + "）");
					out += word;
					continue;
				}
				i = k;	// 跳过整个调用
				std::string body = substitute_params(m.body, m.params, args);
				skip.insert(word);
				out += expand_line(ctx, body, skip);
				skip.erase(word);
			} else {
				out += word;	// 无 '(' → 视为普通标识符
			}
			continue;
		}
		out += c; i++;
	}
	return out;
}

// 条件编译激活判断
static bool cond_active(const std::vector<CondFrame>& conds) {
	if (conds.empty()) return true;
	return conds.back().parent_active && conds.back().taken;
}

// 分割宏参数列表（"a, b, c" → 3 项）
static void split_params(const std::string& s, std::vector<std::string>& out) {
	std::string cur;
	for (char c : s) {
		if (c == ',') { out.push_back(trim(cur)); cur.clear(); }
		else cur += c;
	}
	if (!trim(cur).empty()) out.push_back(trim(cur));
}

// 处理一条指令（t 以 '#' 开头）
static void handle_directive(PreCtx& ctx, const std::string& t,
                             const std::string& fname, int line_no,
                             std::vector<CondFrame>& conds, std::string& out) {
	(void)out;
	size_t p = 1;
	while (p < t.size() && std::isspace((unsigned char)t[p])) p++;
	if (p >= t.size()) return;		// 空 '#'

	size_t q = p;
	while (q < t.size() && !std::isspace((unsigned char)t[q]) && t[q] != '(') q++;
	std::string dir = t.substr(p, q - p);
	std::string rest = t.substr(q);

	auto err = [&](const std::string& msg) {
		ctx.errs.push_back(fname + ":" + std::to_string(line_no) + ": " + msg);
	};

	if (dir == "ifdef" || dir == "ifndef") {
		std::string name = trim(rest);
		bool parent = cond_active(conds);
		bool def = ctx.macros.count(name) > 0;

		bool taken = (dir == "ifdef") ? (parent && def) : (parent && !def);
		conds.push_back({parent, taken, false});
		return;
	}
	if (dir == "else") {
		if (conds.empty()) { err("#else 无对应 #if"); return; }
		CondFrame& f = conds.back();
		if (f.has_else) { err("重复的 #else"); return; }
		f.taken = f.parent_active && !f.taken;
		f.has_else = true;
		return;
	}
	if (dir == "endif") {
		if (conds.empty()) { err("#endif 无对应 #if"); return; }
		conds.pop_back();
		return;
	}
	// 非激活分支中的其它指令忽略
	if (!cond_active(conds)) return;

	if (dir == "include") {
		std::string inc = trim(rest);
		bool angle = false;
		std::string name;
		if (inc.size() >= 2 && inc[0] == '<') {
			size_t e = inc.find('>');
			if (e == std::string::npos) { err("#include 缺少 '>'"); return; }
			name = inc.substr(1, e - 1);
			angle = true;
		} else if (inc.size() >= 2 && inc[0] == '"') {
			size_t e = inc.find('"', 1);
			if (e == std::string::npos) { err("#include 缺少 '\"'"); return; }
			name = inc.substr(1, e - 1);
			angle = false;
		} else {
			err("#include 需要 <foo.h> 或 \"foo.h\"");
			return;
		}
		std::string path;
		if (angle) {
			path = join_path(ctx.include_dir, name);
		} else {
			path = join_path(dir_name(fname), name);
			if (!file_exists(path)) path = join_path(ctx.include_dir, name);
		}
		std::string content;
		if (!read_file(path, content)) {
			err("无法打开 #include 文件: " + name);
			return;
		}
		// 记录头文件 basename（去扩展名），供 lib 自动查找
		{
			std::string base = name;
			size_t dot = base.rfind('.');
			if (dot != std::string::npos) base = base.substr(0, dot);
			ctx.includes.insert(base);
		}
		if (ctx.depth >= 32) { err("包含嵌套过深（可能循环包含）"); return; }
		ctx.depth++;
		process_file_impl(ctx, content, path, out);
		ctx.depth--;
		return;
	}
	if (dir == "define") {
		std::string r = trim(rest);
		size_t p0 = 0;
		while (p0 < r.size() && !std::isspace((unsigned char)r[p0]) && r[p0] != '(') p0++;
		std::string name = r.substr(0, p0);
		if (name.empty() || !is_ident_start(name[0])) { err("非法宏名"); return; }
		Macro m;
		m.is_func = false;
		if (p0 < r.size() && r[p0] == '(') {
			// 函数宏：解析参数表
			m.is_func = true;
			size_t i = p0 + 1;
			int depth = 1;
			std::string params;
			while (i < r.size() && depth > 0) {
				char c = r[i];
				if (c == '(') depth++;
				else if (c == ')') { depth--; if (depth == 0) break; }
				params += c; i++;
			}
			if (depth != 0) { err("宏 " + name + " 参数表未闭合"); return; }
			split_params(params, m.params);
			i++;	// 跳过 ')'
			m.body = trim(r.substr(i));
		} else {
			m.body = trim(r.substr(p0));
		}
		// 允许重新定义（直接覆盖）
		ctx.macros[name] = m;
		return;
	}
	if (dir == "undef") {
		ctx.macros.erase(trim(rest));
		return;
	}
	if (dir == "pragma") {
		return;		// 忽略
	}
	err("不支持的预处理指令: #" + dir);
}

// 处理一个文件的内容（共享宏表；条件栈每文件独立）
void process_file_impl(PreCtx& ctx, const std::string& text,
                       const std::string& fname, std::string& out) {
	std::string clean = strip_comments(text);
	std::vector<CondFrame> conds;
	std::set<std::string> skip;
	std::istringstream ss(clean);
	std::string line;
	int line_no = 0;
	while (std::getline(ss, line)) {
		line_no++;
		std::string t = trim(line);
		if (t.empty()) {
			if (cond_active(conds)) out += "\n";
			continue;
		}
		if (t[0] == '#') {
			handle_directive(ctx, t, fname, line_no, conds, out);
			continue;
		}
		if (cond_active(conds)) {
			out += expand_line(ctx, line, skip);
			out += "\n";
		}
	}
	if (!conds.empty()) {
		ctx.errs.push_back(fname + ": 未闭合的 #if（缺少 #endif）");
	}
}

} // namespace

PreprocessResult preprocess(const std::string& src, const std::string& filename,
                            const std::string& include_dir) {
	PreCtx ctx;
	ctx.include_dir = include_dir;
	PreprocessResult res;
	process_file_impl(ctx, src, filename, res.text);
	res.errs = std::move(ctx.errs);
	res.includes.assign(ctx.includes.begin(), ctx.includes.end());
	return res;
}

} // namespace dcc
