#include "src/preprocessor.h"
#include <cstdio>
int main() {
	std::string src = "#include <stdint.h>\n#include <io.h>\nint main(void){return 0;}\n";
	auto r = dcc::preprocess(src, "t.c", "/home/hangco/doctor-emu/dcc/include");
	for (auto& e : r.errs) std::printf("ERR: %s\n", e.c_str());
	// 打印前 12 行
	size_t pos = 0; int n = 0;
	while (pos < r.text.size() && n < 200) {
		size_t nl = r.text.find('\n', pos);
		std::string line = r.text.substr(pos, nl==std::string::npos?std::string::npos:nl-pos);
		std::printf("%3d: %s\n", n+1, line.c_str());
		if (nl==std::string::npos) break;
		pos = nl+1; n++;
	}
	return 0;
}
