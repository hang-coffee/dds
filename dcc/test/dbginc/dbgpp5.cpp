#include "src/preprocessor.h"
#include <cstdio>
int main() {
	std::string src = "#include <io.h>\nuint16_t myfn(uint16_t port) { return 1; }\n";
	auto r = dcc::preprocess(src, "t.c", "/home/hangco/doctor-emu/dcc/include");
	int lines = 0;
	for (char c : r.text) if (c == '\n') lines++;
	std::printf("total newlines=%d size=%zu\n", lines, r.text.size());
	return 0;
}
