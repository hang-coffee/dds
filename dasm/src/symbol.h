#ifndef DASM_SYMBOL_H
#define DASM_SYMBOL_H

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace dasm {

// ---- 段类型 ----
typedef enum {
    SEG_TEXT,
    SEG_DATA,
} segment_type;

// ---- 单个符号 ----
struct symbol {
    std::string name;
    uint32_t address;        // 段内偏移
    segment_type segment;    // 属于哪个段
    bool resolved;           // 是否已解析（第一遍后为 true）
};

// ---- 符号表 ----
struct symbol_table {
    // 标号名 -> 符号信息
    std::unordered_map<std::string, symbol> symbols;

    // 当前段偏移（第一遍累加用）
    uint32_t text_offset;
    uint32_t data_offset;

    // 当前段基址（由 ORG 设置，默认 0x0000）
    uint32_t text_base;
    uint32_t data_base;

    // 当前正在处理的段
    segment_type current_segment;

    // 错误收集
    std::vector<std::string> errors;
};

// ---- 初始化 ----
void symbol_init(symbol_table& symtab);

// ---- 重置偏移/基址/当前段（不清空符号表；第二遍扫描前调用） ----
void symbol_reset_offsets(symbol_table& symtab);

// ---- 切换段 ----
void symbol_set_segment(symbol_table& symtab, segment_type seg);

// ---- 设置 ORG（段基址） ----
void symbol_set_org(symbol_table& symtab, segment_type seg, uint32_t base);

// ---- 添加标号 ----
// 在当前段当前位置添加一个标号
// 如果标号已存在，记录错误并返回 false
bool symbol_add_label(symbol_table& symtab, const std::string& name);

// ---- 查询标号 ----
// 返回符号指针，若不存在返回 nullptr
const symbol* symbol_lookup(const symbol_table& symtab, const std::string& name);

// ---- 获取当前地址（用于 $） ----
uint32_t symbol_get_current_address(const symbol_table& symtab);

// ---- 累加段偏移 ----
// 在生成机器码时，每生成一个字节，调用此函数累加偏移
void symbol_advance(symbol_table& symtab, uint32_t bytes);

// ---- 对齐（用于 RESB） ----
// 将当前段偏移对齐到 N 字节边界
void symbol_align(symbol_table& symtab, uint32_t alignment);

// ---- 获取错误信息 ----
const std::vector<std::string>& symbol_get_errors(const symbol_table& symtab);

// ---- 清空错误 ----
void symbol_clear_errors(symbol_table& symtab);

// ---- 调试：打印符号表 ----
void symbol_print(const symbol_table& symtab);
void symbol_print_to_file(const symbol_table& symtab, FILE* out);

} // namespace dasm

#endif
