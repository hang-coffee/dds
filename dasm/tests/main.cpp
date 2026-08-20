#include "symbol.h"
#include <iostream>

int main() {
    dasm::symbol_table symtab;
    dasm::symbol_init(symtab);

    // 测试添加标号
    dasm::symbol_add_label(symtab, "main");
    dasm::symbol_advance(symtab, 4);   // 假设 main 占 4 字节
    dasm::symbol_add_label(symtab, "loop");
    dasm::symbol_advance(symtab, 8);
    dasm::symbol_add_label(symtab, "exit");

    // 测试 ORG
    dasm::symbol_set_org(symtab, dasm::SEG_DATA, 0x1000);
    dasm::symbol_set_segment(symtab, dasm::SEG_DATA);
    dasm::symbol_add_label(symtab, "data_start");
    dasm::symbol_advance(symtab, 2);
    dasm::symbol_add_label(symtab, "data_end");

    // 测试查询
    const dasm::symbol* sym = dasm::symbol_lookup(symtab, "loop");
    if (sym) {
        std::cout << "loop 地址: 0x" << std::hex << sym->address << std::dec << std::endl;
    }

    // 测试 $ 展开
    uint32_t current = dasm::symbol_get_current_address(symtab);
    std::cout << "当前地址 (DATA): 0x" << std::hex << current << std::dec << std::endl;

    // 打印符号表
    dasm::symbol_print(symtab);

    // 测试重复定义错误
    dasm::symbol_add_label(symtab, "main");
    if (!dasm::symbol_get_errors(symtab).empty()) {
        std::cout << "错误: " << dasm::symbol_get_errors(symtab)[0] << std::endl;
    }

    return 0;
}
