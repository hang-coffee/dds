#include "preprocessor.h"
#include "lexer.h"
#include "parser.h"
#include "generator.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>


static std::string replace_ext(const std::string& path, const std::string& newext) {
	size_t dot = path.rfind('.');
	size_t slash = path.rfind('/');
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
		return path.substr(0, dot) + newext;
	return path + newext;
}


int main(int argc, char* argv[]) {
    std::string format = "bin";
    std::string table_file;
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-m" || a == "--format") {
            if (i + 1 < argc) format = argv[++i];
            continue;
        }
        if (a == "-t" || a == "--table") {
            if (i + 1 < argc) table_file = argv[++i];
            continue;
        }
        if (a == "-h" || a == "--help") {
            std::cerr << "用法: " << argv[0] << " [选项] <input.asm> [output]" << std::endl;
            std::cerr << "  -m, --format <bin|elf>   输出格式（默认 bin）" << std::endl;
            std::cerr << "  -t, --table <file>       符号表输出到文件（默认不输出）" << std::endl;
            return 0;
        }
        args.push_back(a);
    }
    if (args.empty()) {
        std::cerr << "用法: " << argv[0] << " [选项] <input.asm> [output]" << std::endl;
        return 1;
    }

    std::string input_file = args[0];
    std::string code_file = (args.size() > 1) ? args[1] : (format == "elf" ? replace_ext(input_file, ".o") : "code.bin");
    std::string data_file = (args.size() > 2) ? args[2] : "data.bin";

    // 1. 预处理
    dasm::preprocessor_context pctx;
    pctx.current_source_dir = ".";
    auto lines = dasm::preprocess_file(input_file, pctx);
	
#ifdef DEBUG
	std::cerr << "预处理后共 " << lines.size() << " 行，内容如下：" << std::endl;
	for (size_t i = 0; i < lines.size(); ++i) {
    	std::cerr << "[" << i << "] (行 " << lines[i].first << ") " << lines[i].second << std::endl;
	}
#endif

    if (lines.empty()) {
        std::cerr << "预处理后无有效代码" << std::endl;
        return 1;
    }

    // 2. 词法分析
    dasm::lexer_context lctx;
    dasm::lexer_init(lctx, lines);

    std::vector<dasm::token> tokens;
    dasm::token tok;
    while (dasm::lexer_next_token(lctx, tok)) {
        if (tok.type == dasm::TOK_EOF) break;
        tokens.push_back(tok);
    }

    if (tokens.empty()) {
        std::cerr << "词法分析无 Token" << std::endl;
        return 1;
    }

    // 3. 汇编（两遍扫描）
    dasm::symbol_table symtab;
    dasm::generator_context gen;
    dasm::parser_context pctx2;
    dasm::parser_init(pctx2, tokens, symtab, gen);

    bool verbose = false;
    if (!dasm::parser_assemble(pctx2, verbose)) {
        std::cerr << "汇编失败" << std::endl;
        return 1;
    }

    // 符号表输出
    if (!table_file.empty()) {
        FILE* tf = fopen(table_file.c_str(), "w");
        if (tf) {
            dasm::symbol_print_to_file(symtab, tf);
            fclose(tf);
        } else {
            std::cerr << "无法写入符号表文件: " << table_file << std::endl;
            return 1;
        }
    }

    // 4. 输出文件
    if (format == "elf") {
        if (!dasm::generator_write_elf(gen, symtab, code_file)) {
            std::cerr << "写入 ELF 文件失败" << std::endl;
            return 1;
        }
        std::cout << "成功生成 ELF: " << code_file << " (" << gen.code_buffer.size() << " 字节 text, "
                  << gen.data_buffer.size() << " 字节 data)" << std::endl;
    } else {
        if (!dasm::generator_write_files(gen, code_file, data_file)) {
            std::cerr << "写入文件失败" << std::endl;
            return 1;
        }
        std::cout << "成功生成: " << code_file << " (" << gen.code_buffer.size() << " 字节)"
                  << ", " << data_file << " (" << gen.data_buffer.size() << " 字节)"
                  << std::endl;
    }

    return 0;
}
