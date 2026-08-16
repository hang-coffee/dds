#include "generator.h"
#include <fstream>
#include <iostream>

namespace dasm {

void generator_init(generator_context& gen) {
    gen.code_buffer.clear();
    gen.data_buffer.clear();
    gen.current_segment = SEG_TEXT;
}

void generator_set_segment(generator_context& gen, segment_type seg) {
    gen.current_segment = seg;
}

void generator_emit_byte(generator_context& gen, unsigned char byte) {
    if (gen.current_segment == SEG_TEXT) {
        gen.code_buffer.push_back(byte);
    } else {
        gen.data_buffer.push_back(byte);
    }
}

void generator_emit_bytes(generator_context& gen, const unsigned char* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        generator_emit_byte(gen, data[i]);
    }
}

void generator_emit_immediate(generator_context& gen, uint32_t value, uint32_t bytes) {
    for (uint32_t i = 0; i < bytes; i++) {
        generator_emit_byte(gen, static_cast<unsigned char>((value >> (i * 8)) & 0xFF));
    }
}

void generator_emit_data(generator_context& gen, const unsigned char* data, size_t len) {
    generator_emit_bytes(gen, data, len);
}

void generator_emit_reserve(generator_context& gen, uint32_t bytes) {
    for (uint32_t i = 0; i < bytes; i++) {
        generator_emit_byte(gen, 0x00);
    }
}

void generator_pad_to(generator_context& gen, size_t target) {
    std::vector<unsigned char>& buf = (gen.current_segment == SEG_TEXT)
        ? gen.code_buffer : gen.data_buffer;
    if (buf.size() < target) {
        buf.resize(target, 0x00);
    }
}

bool generator_write_files(const generator_context& gen,
                           const std::string& code_file,
                           const std::string& data_file) {
    std::ofstream code_out(code_file, std::ios::binary);
    if (!code_out.is_open()) {
        std::cerr << "无法写入 " << code_file << std::endl;
        return false;
    }
    code_out.write(reinterpret_cast<const char*>(gen.code_buffer.data()),
                   gen.code_buffer.size());
    code_out.close();

    std::ofstream data_out(data_file, std::ios::binary);
    if (!data_out.is_open()) {
        std::cerr << "无法写入 " << data_file << std::endl;
        return false;
    }
    data_out.write(reinterpret_cast<const char*>(gen.data_buffer.data()),
                   gen.data_buffer.size());
    data_out.close();

    return true;
}

} // namespace dasm
