#include "src/preprocessor.h"
#include <cstdio>
int main() {
	std::string src;
	FILE* f = fopen("lib/io.c", "rb");
	fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
	src.resize(sz); fread(&src[0], 1, sz, f); fclose(f);
	auto r = dcc::preprocess(src, "lib/io.c", "/home/hangco/doctor-emu/dcc/include");
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
