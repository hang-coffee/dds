#include "preprocessor.h"
#include "lexer.h"
#include "parser.h"
#include "generator.h"
#include <iostream>
#include <string>
#include <cstdlib>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <input.asm> [code.bin] [data.bin]" << std::endl;
        return 1;
    }

    std::string input_file = argv[1];
    std::string code_file = (argc > 2) ? argv[2] : "code.bin";
    std::string data_file = (argc > 3) ? argv[3] : "data.bin";

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

    bool verbose = true;
    if (!dasm::parser_assemble(pctx2, verbose)) {
        std::cerr << "汇编失败" << std::endl;
        return 1;
    }

    // 4. 输出文件
    if (!dasm::generator_write_files(gen, code_file, data_file)) {
        std::cerr << "写入文件失败" << std::endl;
        return 1;
    }

    std::cout << "成功生成: " << code_file << " (" << gen.code_buffer.size() << " 字节)"
              << ", " << data_file << " (" << gen.data_buffer.size() << " 字节)"
              << std::endl;

    return 0;
}
