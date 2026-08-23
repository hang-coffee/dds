#include "generator.h"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <map>

namespace dasm {

void generator_init(generator_context& gen) {
    gen.code_buffer.clear();
    gen.data_buffer.clear();
    gen.current_segment = SEG_TEXT;
    gen.relocations.clear();
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

void generator_add_relocation(generator_context& gen, const std::string& symbol,
                              uint32_t offset, segment_type segment, uint32_t width) {
    relocation_entry rel;
    rel.symbol = symbol;
    rel.segment = segment;
    rel.offset = offset;
    rel.width = width;
    gen.relocations.push_back(rel);
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

    // 符号列表：先本地定义符号，再外部 EXTERN 符号
    struct SymInfo { const symbol* sym; std::string name; };
    std::vector<SymInfo> syms;
    for (const auto& pair : symtab.symbols) syms.push_back({&pair.second, pair.first});
    std::sort(syms.begin(), syms.end(), [](const SymInfo& a, const SymInfo& b) {
        if (a.sym->external != b.sym->external) return !a.sym->external;
        if (a.sym->external) return a.name < b.name;
        if (a.sym->segment != b.sym->segment) return a.sym->segment < b.sym->segment;
        return a.sym->address < b.sym->address;
    });

    // 段名字符串表
    std::string shstr(1, '\0');
    auto add_shstr = [&](const std::string& name) -> uint32_t {
        uint32_t off = (uint32_t)shstr.size();
        shstr += name;
        shstr += '\0';
        return off;
    };
    uint32_t shstr_text_off   = add_shstr(".text");
    uint32_t shstr_data_off   = add_shstr(".data");
    uint32_t shstr_sym_off    = add_shstr(".symtab");
    uint32_t shstr_str_off    = add_shstr(".strtab");
    uint32_t shstr_shstr_off  = add_shstr(".shstrtab");

    bool has_rel_text = false;
    bool has_rel_data = false;
    for (const auto& rel : gen.relocations) {
        if (rel.segment == SEG_TEXT) has_rel_text = true;
        else has_rel_data = true;
    }
    uint32_t shstr_reltext_off = 0;
    uint32_t shstr_reldata_off = 0;
    if (has_rel_text) shstr_reltext_off = add_shstr(".rel.text");
    if (has_rel_data) shstr_reldata_off = add_shstr(".rel.data");

    size_t shstr_off = out.size();
    out.insert(out.end(), shstr.begin(), shstr.end());

    // 符号名字符串表
    size_t str_off = out.size();
    std::string strtab = std::string("\0", 1);
    for (const auto& si : syms) strtab += si.name + '\0';
    out.insert(out.end(), strtab.begin(), strtab.end());

    // 符号表
    size_t sym_off = out.size();
    for (int i = 0; i < 16; i++) out.push_back(0);
    uint32_t name_off = 1;
    size_t first_global = syms.size() + 1; // 默认没有全局符号
    for (size_t i = 0; i < syms.size(); i++) {
        const symbol& sym = *syms[i].sym;
        uint32_t size = 0;
        if (!sym.external) {
            for (size_t j = i + 1; j < syms.size(); j++) {
                if (syms[j].sym->external) break;
                if (syms[j].sym->segment == sym.segment) {
                    size = syms[j].sym->address - sym.address;
                    break;
                }
            }
            if (size == 0) {
                uint32_t sec_size = (sym.segment == SEG_TEXT) ? (uint32_t)gen.code_buffer.size() : (uint32_t)gen.data_buffer.size();
                if (sym.address < sec_size) size = sec_size - sym.address;
            }
        }
        put32(out, name_off);
        put32(out, sym.external ? 0 : sym.address);
        put32(out, size);
        if (sym.external) {
            out.push_back(0x10); // STB_GLOBAL | STT_NOTYPE
            first_global = std::min(first_global, i + 1);
        } else {
			bool is_local=(syms[i].name.compare(0, 2, ".L")==0);
			unsigned char type=(sym.segment==SEG_TEXT)?2:1; // STT_FUNC / STT_OBJECT
            unsigned char bind=is_local?0:1;
			unsigned char st_info=(bind<<4)|type;
			out.push_back(st_info);
			if(!is_local) {
				first_global=std::min(first_global, i+1);
			}
        }
        out.push_back(0);
        put16(out, sym.external ? 0 : (sym.segment == SEG_TEXT ? 1 : 2));
        name_off += (uint32_t)syms[i].name.size() + 1;
    }

    // 符号名 -> 符号表索引（+1 跳过空符号）
    std::map<std::string, uint32_t> sym_index;
    for (size_t i = 0; i < syms.size(); i++) {
        sym_index[syms[i].name] = (uint32_t)i + 1;
    }

    // 重定位段数据
    auto build_relocs = [&](segment_type seg) -> std::vector<unsigned char> {
        std::vector<unsigned char> rel_data;
        for (const auto& rel : gen.relocations) {
            if (rel.segment != seg) continue;
            auto it = sym_index.find(rel.symbol);
            if (it == sym_index.end()) continue;
            uint32_t r_type = 1; // R_386_32
            if (rel.width == 2) r_type = 20;  // R_386_16
            if (rel.width == 1) r_type = 22;  // R_386_8
            uint32_t r_info = (it->second << 8) | r_type;
            put32(rel_data, rel.offset);
            put32(rel_data, r_info);
        }
        return rel_data;
    };
    std::vector<unsigned char> rel_text_data = has_rel_text ? build_relocs(SEG_TEXT) : std::vector<unsigned char>();
    std::vector<unsigned char> rel_data_data = has_rel_data ? build_relocs(SEG_DATA) : std::vector<unsigned char>();
    size_t rel_text_off = out.size();
    out.insert(out.end(), rel_text_data.begin(), rel_text_data.end());
    size_t rel_data_off = out.size();
    out.insert(out.end(), rel_data_data.begin(), rel_data_data.end());

    size_t shoff = out.size();
    size_t shnum = 6 + (has_rel_text ? 1 : 0) + (has_rel_data ? 1 : 0);
    auto shdr = [&](uint32_t name, uint32_t type, uint32_t flags, uint32_t addr,
                    uint32_t offset, uint32_t size, uint32_t link, uint32_t info,
                    uint32_t addralign, uint32_t entsize) {
        put32(out, name); put32(out, type); put32(out, flags); put32(out, addr);
        put32(out, offset); put32(out, size); put32(out, link); put32(out, info);
        put32(out, addralign); put32(out, entsize);
    };
    shdr(0,0,0,0,0,0,0,0,0,0);
    shdr(shstr_text_off, 1, 0x6, 0, (uint32_t)text_off, (uint32_t)gen.code_buffer.size(), 0,0,4,0);
    shdr(shstr_data_off, 1, 0x3, 0, (uint32_t)data_off, (uint32_t)gen.data_buffer.size(), 0,0,4,0);
    shdr(shstr_sym_off, 2, 0, 0, (uint32_t)sym_off, (uint32_t)((1 + syms.size()) * 16), 4, (uint32_t)first_global, 4, 16);
    shdr(shstr_str_off, 3, 0, 0, (uint32_t)str_off, (uint32_t)strtab.size(), 0,0,1,0);
    shdr(shstr_shstr_off, 3, 0, 0, (uint32_t)shstr_off, (uint32_t)shstr.size(), 0,0,1,0);
    if (has_rel_text) {
        shdr(shstr_reltext_off, 9, 0, 0, (uint32_t)rel_text_off, (uint32_t)rel_text_data.size(), 3, 1, 4, 8);
    }
    if (has_rel_data) {
        shdr(shstr_reldata_off, 9, 0, 0, (uint32_t)rel_data_off, (uint32_t)rel_data_data.size(), 3, 2, 4, 8);
    }

    uint32_t shoff32 = (uint32_t)shoff;
    out[shoff_pos] = (unsigned char)(shoff32 & 0xff);
    out[shoff_pos+1] = (unsigned char)((shoff32 >> 8) & 0xff);
    out[shoff_pos+2] = (unsigned char)((shoff32 >> 16) & 0xff);
    out[shoff_pos+3] = (unsigned char)((shoff32 >> 24) & 0xff);
    put16(out, shnum); // e_shnum at offset 48? Need patch bytes 48-49.
    // e_shnum is at file offset 48 (after e_shoff 32-35, e_shstrndx 50-51)
    out[48] = (unsigned char)(shnum & 0xff);
    out[49] = (unsigned char)((shnum >> 8) & 0xff);
    // e_shstrndx at 50-51: index of .shstrtab = 5
    out[50] = 5;
    out[51] = 0;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(out.data()), out.size());
    f.close();
    return true;
}

} // namespace dasm
