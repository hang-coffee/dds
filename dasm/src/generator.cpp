#include "generator.h"
#include <fstream>
#include <algorithm>
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


static void put16(std::vector<unsigned char>& v, uint16_t x) {
v.push_back((unsigned char)(x & 0xff));
v.push_back((unsigned char)((x >> 8) & 0xff));
}

static void put32(std::vector<unsigned char>& v, uint32_t x) {
v.push_back((unsigned char)(x & 0xff));
v.push_back((unsigned char)((x >> 8) & 0xff));
v.push_back((unsigned char)((x >> 16) & 0xff));
v.push_back((unsigned char)((x >> 24) & 0xff));
}

bool generator_write_elf(const generator_context& gen, const symbol_table& symtab, const std::string& path) {
std::vector<unsigned char> out;
out.insert(out.end(), {0x7f,'E','L','F',1,1,1,0,0,0,0,0,0,0,0,0});
put16(out, 1);
put16(out, 0xFFFF); // 自定义保留机器号，避免误识别为具体架构
put32(out, 1);
put32(out, 0);
put32(out, 0);
size_t shoff_pos = out.size();
put32(out, 0);
put32(out, 0);
put16(out, 52);
put16(out, 0);
put16(out, 0);
put16(out, 40);
put16(out, 6);
put16(out, 5);

size_t text_off = out.size();
out.insert(out.end(), gen.code_buffer.begin(), gen.code_buffer.end());
size_t data_off = out.size();
out.insert(out.end(), gen.data_buffer.begin(), gen.data_buffer.end());

// 按段和地址排序符号
std::vector<std::pair<const symbol*, std::string>> syms;
for (const auto& pair : symtab.symbols) syms.push_back({&pair.second, pair.first});
std::sort(syms.begin(), syms.end(), [](const auto& a, const auto& b) {
if (a.first->segment != b.first->segment) return a.first->segment < b.first->segment;
return a.first->address < b.first->address;
});

size_t shstr_off = out.size();
std::string shstr(1, '\0');
shstr += ".text"; shstr += '\0';
shstr += ".data"; shstr += '\0';
shstr += ".symtab"; shstr += '\0';
shstr += ".strtab"; shstr += '\0';
shstr += ".shstrtab"; shstr += '\0';
out.insert(out.end(), shstr.begin(), shstr.end());

size_t str_off = out.size();
std::string strtab = std::string("\0", 1);
for (const auto& pair : syms) strtab += pair.second + '\0';
out.insert(out.end(), strtab.begin(), strtab.end());

size_t sym_off = out.size();
for (int i = 0; i < 16; i++) out.push_back(0);
uint32_t name_off = 1;
for (size_t i = 0; i < syms.size(); i++) {
const symbol& sym = *syms[i].first;
uint32_t size = 0;
for (size_t j = i + 1; j < syms.size(); j++) {
if (syms[j].first->segment == sym.segment) {
size = syms[j].first->address - sym.address;
break;
}
}
if (size == 0) {
uint32_t sec_size = (sym.segment == SEG_TEXT) ? (uint32_t)gen.code_buffer.size() : (uint32_t)gen.data_buffer.size();
if (sym.address < sec_size) size = sec_size - sym.address;
}
put32(out, name_off);
put32(out, sym.address);
put32(out, size);
out.push_back(sym.segment == SEG_TEXT ? 0x12 : 0x11);
out.push_back(0);
put16(out, sym.segment == SEG_TEXT ? 1 : 2);
name_off += (uint32_t)syms[i].second.size() + 1;
}

size_t shoff = out.size();
size_t shstr_name_off = 1;
size_t shstr_data_off = shstr_name_off + 6;
size_t shstr_sym_off = shstr_data_off + 6;
size_t shstr_str_off = shstr_sym_off + 8;
size_t shstr_shstr_off = shstr_str_off + 8;
auto shdr = [&](uint32_t name, uint32_t type, uint32_t flags, uint32_t addr,
                uint32_t offset, uint32_t size, uint32_t link, uint32_t info,
                uint32_t addralign, uint32_t entsize) {
put32(out, name); put32(out, type); put32(out, flags); put32(out, addr);
put32(out, offset); put32(out, size); put32(out, link); put32(out, info);
put32(out, addralign); put32(out, entsize);
};
shdr(0,0,0,0,0,0,0,0,0,0);
shdr((uint32_t)shstr_name_off, 1, 0x6, 0, (uint32_t)text_off, (uint32_t)gen.code_buffer.size(), 0,0,4,0);
shdr((uint32_t)shstr_data_off, 1, 0x3, 0, (uint32_t)data_off, (uint32_t)gen.data_buffer.size(), 0,0,4,0);
shdr((uint32_t)shstr_sym_off, 2, 0, 0, (uint32_t)sym_off, (uint32_t)((1 + syms.size()) * 16), 4, 1, 4, 16);
shdr((uint32_t)shstr_str_off, 3, 0, 0, (uint32_t)str_off, (uint32_t)strtab.size(), 0,0,1,0);
shdr((uint32_t)shstr_shstr_off, 3, 0, 0, (uint32_t)shstr_off, (uint32_t)shstr.size(), 0,0,1,0);
uint32_t shoff32 = (uint32_t)shoff;
out[shoff_pos] = (unsigned char)(shoff32 & 0xff);
out[shoff_pos+1] = (unsigned char)((shoff32 >> 8) & 0xff);
out[shoff_pos+2] = (unsigned char)((shoff32 >> 16) & 0xff);
out[shoff_pos+3] = (unsigned char)((shoff32 >> 24) & 0xff);
std::ofstream f(path, std::ios::binary);
if (!f) return false;
f.write(reinterpret_cast<const char*>(out.data()), out.size());
f.close();
return true;
}

} // namespace dasm
