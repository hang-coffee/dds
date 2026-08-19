#include "src/preprocessor.h"
#include "src/lexer.h"
#include "src/parser.h"
#include <cstdio>
int main() {
	std::string src = "#include <stdint.h>\n#include <io.h>\nuint16_t inb(uint16_t port) { return port; }\n";
	auto r = dcc::preprocess(src, "t.c", "/home/hangco/doctor-emu/tools/dcc/include");
	for (auto& e : r.errs) std::printf("PP: %s\n", e.c_str());
	dcc::Lexer lx(r.text);
	auto toks = lx.tokenize();
	for (auto& e : lx.errors()) std::printf("LX: %s\n", e.c_str());
	dcc::TypeEnv env;
	dcc::Parser p(toks, env);
	auto prog = p.parse();
	for (auto& e : p.errors()) std::printf("PS: %s\n", e.c_str());
	for (auto& f : prog.funcs) std::printf("  func %s decl=%d\n", f.name.c_str(), f.is_decl);
	return 0;
}
