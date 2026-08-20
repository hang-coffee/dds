#include "symbol.h"
#include <iostream>
#include <cstdio>
#include <iomanip>

namespace dasm {

// ---- 初始化 ----
void symbol_init(symbol_table& symtab) {
    symtab.symbols.clear();
    symtab.text_offset = 0;
    symtab.data_offset = 0;
    symtab.text_base = 0;
    symtab.data_base = 0;
    symtab.current_segment = SEG_TEXT;
    symtab.errors.clear();
}

// ---- 重置偏移/基址/当前段（保留符号表；第二遍扫描使用） ----
void symbol_reset_offsets(symbol_table& symtab) {
    symtab.text_offset = 0;
    symtab.data_offset = 0;
    symtab.text_base = 0;
    symtab.data_base = 0;
    symtab.current_segment = SEG_TEXT;
}

// ---- 切换段 ----
void symbol_set_segment(symbol_table& symtab, segment_type seg) {
    symtab.current_segment = seg;
}

// ---- 设置 ORG ----
void symbol_set_org(symbol_table& symtab, segment_type seg, uint32_t base) {
    if (seg == SEG_TEXT) {
        symtab.text_base = base;
        symtab.text_offset = base;   // ORG 后偏移从基址开始
    } else {
        symtab.data_base = base;
        symtab.data_offset = base;
    }
}

// ---- 添加标号 ----
bool symbol_add_label(symbol_table& symtab, const std::string& name) {
    // 检查是否已存在
    auto it = symtab.symbols.find(name);
    if (it != symtab.symbols.end()) {
        symtab.errors.push_back("标号重复定义: " + name);
        return false;
    }

    symbol sym;
    sym.name = name;
    sym.segment = symtab.current_segment;
    sym.resolved = true;   // 第一遍就确定地址
    sym.external = false;

    // 根据当前段获取地址
    if (symtab.current_segment == SEG_TEXT) {
        sym.address = symtab.text_offset;
    } else {
        sym.address = symtab.data_offset;
    }

    symtab.symbols[name] = sym;
    return true;
}

// ---- 添加外部符号声明 ----
bool symbol_add_extern(symbol_table& symtab, const std::string& name) {
    auto it = symtab.symbols.find(name);
    if (it != symtab.symbols.end()) {
        if (it->second.external) {
            // 重复 EXTERN 声明允许
            return true;
        }
        symtab.errors.push_back("标号与 EXTERN 声明冲突: " + name);
        return false;
    }

    symbol sym;
    sym.name = name;
    sym.segment = SEG_TEXT;
    sym.address = 0;
    sym.resolved = false;
    sym.external = true;
    symtab.symbols[name] = sym;
    return true;
}

// ---- 查询标号 ----
const symbol* symbol_lookup(const symbol_table& symtab, const std::string& name) {
    auto it = symtab.symbols.find(name);
    if (it == symtab.symbols.end()) {
        return nullptr;
    }
    return &it->second;
}

// ---- 获取当前地址（用于 $） ----
uint32_t symbol_get_current_address(const symbol_table& symtab) {
    if (symtab.current_segment == SEG_TEXT) {
        return symtab.text_offset;
    } else {
        return symtab.data_offset;
    }
}

// ---- 累加段偏移 ----
void symbol_advance(symbol_table& symtab, uint32_t bytes) {
    if (symtab.current_segment == SEG_TEXT) {
        symtab.text_offset += bytes;
    } else {
        symtab.data_offset += bytes;
    }
}

// ---- 对齐 ----
void symbol_align(symbol_table& symtab, uint32_t alignment) {
    if (alignment == 0) return;
    uint32_t& offset = (symtab.current_segment == SEG_TEXT) ? symtab.text_offset : symtab.data_offset;
    uint32_t rem = offset % alignment;
    if (rem != 0) {
        offset += (alignment - rem);
    }
}

// ---- 获取错误 ----
const std::vector<std::string>& symbol_get_errors(const symbol_table& symtab) {
    return symtab.errors;
}

// ---- 清空错误 ----
void symbol_clear_errors(symbol_table& symtab) {
    symtab.errors.clear();
}

// ---- 打印符号表 ----
void symbol_print(const symbol_table& symtab) {
    std::cout << "========== 符号表 ==========" << std::endl;
    std::cout << "TEXT 基址: 0x" << std::hex << symtab.text_base
              << ", 偏移: 0x" << symtab.text_offset << std::dec << std::endl;
    std::cout << "DATA 基址: 0x" << std::hex << symtab.data_base
              << ", 偏移: 0x" << symtab.data_offset << std::dec << std::endl;
    std::cout << "当前段: " << (symtab.current_segment == SEG_TEXT ? "TEXT" : "DATA") << std::endl;
    std::cout << std::endl;

    if (symtab.symbols.empty()) {
        std::cout << "(无标号)" << std::endl;
        return;
    }

    std::cout << "标号名           段      地址" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    for (const auto& pair : symtab.symbols) {
        const symbol& sym = pair.second;
        std::cout << std::setw(16) << std::left << sym.name << "  "
                  << (sym.external ? "EXTERN" : (sym.segment == SEG_TEXT ? "TEXT" : "DATA")) << "  "
                  << "0x" << std::hex << sym.address << std::dec << std::endl;
    }
}


void symbol_print_to_file(const symbol_table& symtab, FILE* out) {
fprintf(out, "========== 符号表 ==========\n");
fprintf(out, "TEXT 基址: 0x%X, 偏移: 0x%X\n", symtab.text_base, symtab.text_offset);
fprintf(out, "DATA 基址: 0x%X, 偏移: 0x%X\n", symtab.data_base, symtab.data_offset);
fprintf(out, "当前段: %s\n", symtab.current_segment == SEG_TEXT ? "TEXT" : "DATA");
fprintf(out, "\n");
if (symtab.symbols.empty()) {
fprintf(out, "(无标号)\n");
return;
}
fprintf(out, "标号名           段      地址\n");
fprintf(out, "----------------------------------------\n");
for (const auto& pair : symtab.symbols) {
const symbol& sym = pair.second;
fprintf(out, "%-16s  %-7s 0x%X\n", sym.name.c_str(),
        sym.external ? "EXTERN" : (sym.segment == SEG_TEXT ? "TEXT" : "DATA"), sym.address);
}
}

} // namespace dasm
