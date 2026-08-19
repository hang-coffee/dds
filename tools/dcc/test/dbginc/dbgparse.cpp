#include "src/preprocessor.h"
#include "src/lexer.h"
#include "src/parser.h"
#include <cstdio>
int main() {
	std::string src = "#include <stdint.h>\n#include <io.h>\nint main(void){return 0;}\n";
	auto r = dcc::preprocess(src, "t.c", "/home/hangco/doctor-emu/tools/dcc/include");
	for (auto& e : r.errs) std::printf("PP ERR: %s\n", e.c_str());
	dcc::Lexer lx(r.text);
	auto toks = lx.tokenize();
	for (auto& e : lx.errors()) std::printf("LEX ERR: %s\n", e.c_str());
	dcc::TypeEnv env;
	dcc::Parser p(toks, env);
	auto prog = p.parse();
	for (auto& e : p.errors()) std::printf("PARSE ERR: %s\n", e.c_str());
	std::printf("globals=%zu funcs=%zu\n", prog.globals.size(), prog.funcs.size());
	return 0;
}
