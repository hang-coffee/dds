#ifndef DASM_GENERATOR_H
#define DASM_GENERATOR_H

#include "symbol.h"
#include <vector>
#include <cstdint>
#include <string>

namespace dasm {

struct relocation_entry {
    std::string symbol;      // 外部符号名
    segment_type segment;    // 所在段（TEXT/DATA）
    uint32_t offset;         // 段内偏移（相对段起始）
    uint32_t width;          // 重定位字段宽度：1/2/4
};

struct generator_context {
    std::vector<unsigned char> code_buffer;
    std::vector<unsigned char> data_buffer;
    segment_type current_segment;
    std::vector<relocation_entry> relocations;
};

void generator_init(generator_context& gen);
void generator_set_segment(generator_context& gen, segment_type seg);
void generator_emit_byte(generator_context& gen, unsigned char byte);
void generator_emit_bytes(generator_context& gen, const unsigned char* data, size_t len);
void generator_emit_immediate(generator_context& gen, uint32_t value, uint32_t bytes);
void generator_emit_data(generator_context& gen, const unsigned char* data, size_t len);
void generator_emit_reserve(generator_context& gen, uint32_t bytes);
void generator_add_relocation(generator_context& gen, const std::string& symbol,
                              uint32_t offset, segment_type segment, uint32_t width);
// 将当前段缓冲区填充 0x00 直到长度达到 target（用于 ORG / DB 地址定位）
void generator_pad_to(generator_context& gen, size_t target);
bool generator_write_files(const generator_context& gen, const std::string& code_file, const std::string& data_file);
bool generator_write_elf(const generator_context& gen, const symbol_table& symtab, const std::string& path);

} // namespace dasm

#endif
