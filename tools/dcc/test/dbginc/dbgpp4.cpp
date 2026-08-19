#include "src/preprocessor.h"
#include <cstdio>
int main() {
	std::string src = "#include <io.h>\nuint16_t myfn(uint16_t port) { return 1; }\n";
	auto r = dcc::preprocess(src, "t.c", "/home/hangco/doctor-emu/tools/dcc/include");
	size_t pos = 0; int n = 0;
	while (pos < r.text.size() && n < 60) {
		size_t nl = r.text.find('\n', pos);
		std::string line = r.text.substr(pos, nl==std::string::npos?std::string::npos:nl-pos);
		std::printf("%2d: [%s]\n", n+1, line.c_str());
		if (nl==std::string::npos) break;
		pos = nl+1; n++;
	}
	return 0;
}
