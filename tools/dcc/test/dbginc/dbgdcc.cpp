// 完全复刻 main.cpp 的 compile_one 流程
#include "src/preprocessor.h"
#include "src/lexer.h"
#include "src/parser.h"
#include <cstdio>
int main() {
	std::string path = "lib/io.c";
	std::string src;
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) { std::printf("open fail\n"); return 1; }
	fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
	src.resize(sz); fread(&src[0], 1, sz, f); fclose(f);
	auto r = dcc::preprocess(src, path, "/home/hangco/doctor-emu/tools/dcc/include");
	for (auto& e : r.errs) std::printf("PP: %s\n", e.c_str());
	dcc::Lexer lx(r.text);
	auto toks = lx.tokenize();
	for (auto& e : lx.errors()) std::printf("LX: %s\n", e.c_str());
	dcc::TypeEnv env;
	dcc::Parser p(toks, env);
	auto prog = p.parse();
	for (auto& e : p.errors()) std::printf("PS: %s\n", e.c_str());
	std::printf("globals=%zu funcs=%zu errs=%zu\n", prog.globals.size(), prog.funcs.size(), p.errors().size());
	return 0;
}
