// dlinker.cpp - DOCTOR 最小链接器
// 读取 dasm -m elf 生成的 ELF32 relocatable .o，合并 .text/.data 输出 code/data 二进制。
// 支持 ELF 标准 .rel.text / .rel.data / .rela.text / .rela.data 重定位表，
// 以及兼容旧的“扫描 32 位绝对值”回退逻辑。
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr uint32_t SHT_PROGBITS = 1;
constexpr uint32_t SHT_SYMTAB   = 2;
constexpr uint32_t SHT_STRTAB   = 3;
constexpr uint32_t SHT_RELA     = 4;
constexpr uint32_t SHT_REL      = 9;
constexpr uint16_t SHN_UNDEF    = 0;
constexpr uint16_t SHN_ABS      = 0xFFF1;

struct Section {
    std::string name;
    uint32_t type;
    uint32_t flags;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addralign;
    uint32_t entsize;
    std::vector<uint8_t> data;
};

struct Symbol {
    std::string name;
    uint32_t value;
    uint16_t shndx;
    uint8_t  info; // ELF32 st_info: 高4位 bind, 低4位 type

    bool is_undef() const { return shndx == SHN_UNDEF; }
    bool is_global() const {
        unsigned bind = (info >> 4) & 0xf;
        return bind == 1 || bind == 2; // STB_GLOBAL / STB_WEAK
    }
    bool is_local() const { return ((info >> 4) & 0xf) == 0; }  // STB_LOCAL
};

struct Relocation {
    uint32_t offset;
    uint32_t info;                 // ELF32 r_info: sym<<8 | type
    int32_t  addend;               // RELA 的显式 addend；REL 中不使用
    bool     has_explicit_addend;
    uint32_t target_section;       // sh_info：被重定位的段在 obj.sections 中的下标
};

struct Object {
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
    std::vector<Relocation> relocations;
    std::vector<uint32_t> section_offsets; // 输出段在对象内 text/data 区中的偏移
    size_t text_size = 0;
    size_t data_size = 0;
};

struct ResolvedSymbol {
    std::string name;
    uint32_t address;
    bool defined = false;
};

bool section_is_text(const Section& sec);
bool section_is_data(const Section& sec);

uint32_t rd32(const std::vector<uint8_t>& b, size_t off) {
    return (uint32_t)b[off] | ((uint32_t)b[off+1] << 8) |
           ((uint32_t)b[off+2] << 16) | ((uint32_t)b[off+3] << 24);
}

uint16_t rd16(const std::vector<uint8_t>& b, size_t off) {
    return (uint16_t)((uint16_t)b[off] | ((uint16_t)b[off+1] << 8));
}

bool read_elf(const std::string& path, Object& obj) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 52 || b[0] != 0x7f || b[1] != 'E' || b[2] != 'L' || b[3] != 'F')
        return false;
    uint32_t shoff = rd32(b, 32);
    uint16_t shentsize = rd16(b, 46);
    uint16_t shnum = rd16(b, 48);
    uint16_t shstrndx = rd16(b, 50);
    if (shentsize < 40 || shoff + (size_t)shentsize * shnum > b.size())
        return false;

    auto sh_name = [&](size_t i) -> uint32_t { return rd32(b, shoff + i * shentsize); };
    auto sh_type = [&](size_t i) -> uint32_t { return rd32(b, shoff + i * shentsize + 4); };
    auto sh_flags = [&](size_t i) -> uint32_t { return rd32(b, shoff + i * shentsize + 8); };
    auto sh_offset = [&](size_t i) -> uint32_t { return rd32(b, shoff + i * shentsize + 16); };
    auto sh_size = [&](size_t i) -> uint32_t { return rd32(b, shoff + i * shentsize + 20); };
    auto sh_link = [&](size_t i) -> uint32_t { return rd32(b, shoff + i * shentsize + 24); };
    auto sh_info = [&](size_t i) -> uint32_t { return rd32(b, shoff + i * shentsize + 28); };
    auto sh_addralign = [&](size_t i) -> uint32_t { return rd32(b, shoff + i * shentsize + 32); };
    auto sh_entsize = [&](size_t i) -> uint32_t { return rd32(b, shoff + i * shentsize + 36); };

    std::string shstr;
    if (shstrndx < shnum) {
        uint32_t off = sh_offset(shstrndx), sz = sh_size(shstrndx);
        if (off + sz <= b.size()) shstr.assign((const char*)&b[off], sz);
    }
    for (uint16_t i = 0; i < shnum; i++) {
        uint32_t name_off = sh_name(i);
        std::string name;
        if (name_off < shstr.size()) name = shstr.c_str() + name_off;
        Section sec;
        sec.name = name;
        sec.type = sh_type(i);
        sec.flags = sh_flags(i);
        sec.offset = sh_offset(i);
        sec.size = sh_size(i);
        sec.link = sh_link(i);
        sec.info = sh_info(i);
        sec.addralign = sh_addralign(i);
        sec.entsize = sh_entsize(i);
        if (sec.offset + sec.size <= b.size())
            sec.data.assign(b.begin() + sec.offset, b.begin() + sec.offset + sec.size);
        obj.sections.push_back(sec);
    }
    obj.section_offsets.assign(obj.sections.size(), 0);

    // 解析符号表：保留索引 0（空符号），保证重定位 r_info 能直接索引。
    // 优先通过 symtab 的 sh_link 找到关联的 .strtab，更符合 ELF 规范。
    for (const auto& sec : obj.sections) {
        if (sec.type != SHT_SYMTAB || sec.size < 16 || sec.data.size() < sec.size) continue;
        std::string strtab;
        if (sec.link < obj.sections.size()) {
            const Section& strsec = obj.sections[sec.link];
            if (strsec.type == SHT_STRTAB && !strsec.data.empty())
                strtab.assign((const char*)strsec.data.data(), strsec.data.size());
        }
        if (strtab.empty()) {
            // 兼容个别不以 sh_link 关联的简单文件。
            for (const auto& maybe : obj.sections) {
                if (maybe.type == SHT_STRTAB && maybe.name == ".strtab" && !maybe.data.empty()) {
                    strtab.assign((const char*)maybe.data.data(), maybe.data.size());
                    break;
                }
            }
        }
        for (uint32_t off = 0; off + 16 <= sec.size; off += 16) {
            size_t p = off;
            uint32_t st_name = rd32(sec.data, p);
            uint32_t st_value = rd32(sec.data, p + 4);
            uint32_t st_size = rd32(sec.data, p + 8); // 当前链接器不需要，保留解析占位
            uint8_t st_info = sec.data[p + 12];
            uint8_t st_other = sec.data[p + 13];
            uint16_t st_shndx = rd16(sec.data, p + 14);
            (void)st_size;
            (void)st_other;
            std::string name;
            if (st_name < strtab.size()) name = strtab.c_str() + st_name;
            obj.symbols.push_back({name, st_value, st_shndx, st_info});
        }
    }

    // 解析重定位表。
    for (const auto& sec : obj.sections) {
        bool is_rel = (sec.type == SHT_REL);
        bool is_rela = (sec.type == SHT_RELA);
        if (!is_rel && !is_rela) continue;
        uint32_t entsize = sec.entsize ? sec.entsize : (is_rel ? 8 : 12);
        if (entsize < 8 || sec.data.size() < sec.size) continue;
        for (uint32_t off = 0; off + entsize <= sec.size; off += entsize) {
            Relocation rel;
            rel.offset = rd32(sec.data, off);
            rel.info = rd32(sec.data, off + 4);
            rel.has_explicit_addend = is_rela;
            rel.addend = is_rela ? (int32_t)rd32(sec.data, off + 8) : 0;
            rel.target_section = sec.info;
            obj.relocations.push_back(rel);
        }
    }

    for (const auto& sec : obj.sections) {
        if (section_is_text(sec)) obj.text_size += sec.size;
        else if (section_is_data(sec)) obj.data_size += sec.size;
    }
    return true;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)data.data(), data.size());
    f.close();
    return true;
}

uint32_t read_reloc_field(const std::vector<uint8_t>& buf, size_t pos, unsigned width) {
    uint32_t v = 0;
    for (unsigned i = 0; i < width; i++) {
        if (pos + i >= buf.size()) return 0;
        v |= (uint32_t)buf[pos + i] << (i * 8);
    }
    return v;
}

void write_reloc_field(std::vector<uint8_t>& buf, size_t pos, uint32_t value, unsigned width) {
    for (unsigned i = 0; i < width; i++) {
        if (pos + i < buf.size())
            buf[pos + i] = (uint8_t)((value >> (i * 8)) & 0xff);
    }
}

bool section_is_text(const Section& sec) {
    // 注意：不要把 .rel.text/.rela.text 当成输出段。
    return sec.name == ".text" || sec.name.rfind(".text.", 0) == 0;
}

bool section_is_data(const Section& sec) {
    // 注意：不要把 .rel.data/.rela.data/.rel.rodata 当成输出段。
    return sec.name == ".data" || sec.name.rfind(".data.", 0) == 0 ||
           sec.name == ".rodata" || sec.name.rfind(".rodata.", 0) == 0 ||
           sec.name == ".bss" || sec.name.rfind(".bss.", 0) == 0;
}

uint32_t apply_one_relocation(const Relocation& rel, const Object& obj,
                              std::vector<uint8_t>& code,
                              std::vector<uint8_t>& data,
                              const std::vector<uint32_t>& text_base,
                              const std::vector<uint32_t>& data_base,
                              uint32_t text_output_base,
                              uint32_t data_output_base,
                              size_t obj_index,
                              const std::map<std::string, ResolvedSymbol>& globals,
                              std::string& err) {
    uint32_t r_sym = (rel.info >> 8) & 0x00FFFFFF;
    uint32_t r_type = rel.info & 0xFF;
    if (r_type == 0) return 0; // R_*_NONE

    // 只处理会进入最终 code/data 的段；其它段的 reloc 不参与本链接器。
    if (rel.target_section >= obj.sections.size()) {
        err = "重定位目标段索引越界";
        return 1;
    }
    const Section& target = obj.sections[rel.target_section];
    if (!section_is_text(target) && !section_is_data(target))
        return 0;

    if (r_sym >= obj.symbols.size()) {
        err = "重定位引用了越界的符号表索引 " + std::to_string(r_sym);
        return 1;
    }
    const Symbol& sym = obj.symbols[r_sym];

    // 计算符号的链接后地址。
    uint32_t sym_addr = 0;
    bool have_sym = false;
    if (sym.shndx == SHN_ABS) {
        sym_addr = sym.value;
        have_sym = true;
    } else if (!sym.is_undef()) {
        // 本对象内已定义符号（包括局部符号和全局符号）。
        if (sym.shndx < obj.sections.size()) {
            const Section& sec = obj.sections[sym.shndx];
            if (section_is_text(sec)) {
                sym_addr = text_output_base + text_base[obj_index]
                         + obj.section_offsets[sym.shndx] + sym.value;
                have_sym = true;
            } else if (section_is_data(sec)) {
                sym_addr = data_output_base + data_base[obj_index]
                         + obj.section_offsets[sym.shndx] + sym.value;
                have_sym = true;
            } else {
                err = "符号 " + sym.name + " 所在的段不受支持: " + sec.name;
                return 1;
            }
        } else {
            err = "符号 " + sym.name + " 的段索引越界";
            return 1;
        }
    } else {
        // 未定义符号：必须是由其它 .o 提供的全局符号。
        auto it = globals.find(sym.name);
        if (it == globals.end() || !it->second.defined) {
            err = "未定义符号: " + sym.name;
            return 1;
        }
        sym_addr = it->second.address;
        have_sym = true;
    }
    if (!have_sym) {
        err = "无法解析符号: " + sym.name;
        return 1;
    }

    // 确定被重定位的位置。
    size_t pos;
    std::vector<uint8_t>* buf;
    uint32_t base;
    uint32_t sec_off = obj.section_offsets[rel.target_section];
    if (section_is_text(target)) {
        pos = text_base[obj_index] + sec_off + rel.offset;
        buf = &code;
        base = text_output_base + text_base[obj_index] + sec_off;
    } else {
        pos = data_base[obj_index] + sec_off + rel.offset;
        buf = &data;
        base = data_output_base + data_base[obj_index] + sec_off;
    }

    // 目前 DOCTOR 主要使用 32 位绝对地址；同时兼容常见的 8/16/32 位类型。
    unsigned width = 0;
    bool pc_relative = false;
    switch (r_type) {
        case 0:  return 0; // R_*_NONE
        case 1:  width = 4; break; // R_386_32 / DOCTOR 绝对32
        case 2:  width = 4; pc_relative = true; break; // R_386_PC32
        case 4:  width = 4; pc_relative = true; break; // R_386_PLT32，静态链接按 PC32 处理
        case 20: width = 2; break; // R_386_16
        case 21: width = 2; pc_relative = true; break; // R_386_PC16
        case 22: width = 1; break; // R_386_8
        case 23: width = 1; pc_relative = true; break; // R_386_PC8
        default:
            err = "不支持的重定位类型: " + std::to_string(r_type);
            return 1;
    }

    if (pos + width > buf->size()) {
        err = "重定位位置越界";
        return 1;
    }

    uint32_t addend = rel.has_explicit_addend ? (uint32_t)rel.addend
                                              : read_reloc_field(*buf, pos, width);
    uint32_t value = sym_addr + addend;
    if (pc_relative) {
        uint32_t place = base + rel.offset;
        value = sym_addr + addend - place;
    }
    write_reloc_field(*buf, pos, value, width);
    return 0;
}

// 旧对象没有 .rel.* 时，保留“扫描 32 位绝对值”的简单回退。
// 只扫描当前对象刚拼接进来的那段，避免误改前面对象的字节。
void patch_relocations_fallback(std::vector<uint8_t>& text, std::vector<uint8_t>& data,
                                const Object& obj,
                                uint32_t text_base, uint32_t data_base,
                                uint32_t text_output_base, uint32_t data_output_base) {
    auto patch_range = [&](std::vector<uint8_t>& buf, size_t start, size_t size) {
        size_t end = start + size;
        if (end > buf.size()) end = buf.size();
        for (size_t i = start; i + 4 <= end; i++) {
            uint32_t v = read_reloc_field(buf, i, 4);
            for (const auto& sym : obj.symbols) {
                if (sym.name.empty() || sym.is_undef()) continue;
                if (sym.value == v) {
                    uint32_t sec_off = (sym.shndx < obj.section_offsets.size()) ? obj.section_offsets[sym.shndx] : 0;
                    uint32_t base = (sym.shndx < obj.sections.size() && section_is_data(obj.sections[sym.shndx]))
                                        ? data_output_base + data_base + sec_off
                                        : text_output_base + text_base + sec_off;
                    uint32_t nv = sym.value + base;
                    write_reloc_field(buf, i, nv, 4);
                    break;
                }
            }
        }
    };
    patch_range(text, text_base, obj.text_size);
    patch_range(data, data_base, obj.data_size);
}

std::string base_name(const std::string& path) {
    size_t slash = path.rfind('/');
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = base.rfind('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    return base;
}

bool parse_u32(const std::string& s, uint32_t& out) {
    if (s.empty()) return false;
    try {
        size_t idx = 0;
        unsigned long v = std::stoul(s, &idx, 0);
        if (idx != s.size() || v > 0xFFFFFFFFUL) return false;
        out = (uint32_t)v;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string out_base;
    std::string table_file;
    std::vector<std::string> inputs;
    uint32_t text_output_base = 0;
    uint32_t data_output_base = 0;
    bool verbose = false;
    bool no_fallback = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o") { if (i + 1 < argc) out_base = argv[++i]; continue; }
        if (a == "-t") { if (i + 1 < argc) table_file = argv[++i]; continue; }
        if (a == "-Ttext" || a == "--text-base") {
            if (i + 1 < argc && parse_u32(argv[++i], text_output_base)) continue;
            std::cerr << "无效的代码段基址: " << (i < argc ? argv[i] : "") << std::endl;
            return 1;
        }
        if (a.rfind("--text-base=", 0) == 0) {
            if (parse_u32(a.substr(12), text_output_base)) continue;
            std::cerr << "无效的代码段基址: " << a << std::endl;
            return 1;
        }
        if (a.rfind("-Ttext=", 0) == 0) {
            if (parse_u32(a.substr(7), text_output_base)) continue;
            std::cerr << "无效的代码段基址: " << a << std::endl;
            return 1;
        }
        if (a == "-Tdata" || a == "--data-base") {
            if (i + 1 < argc && parse_u32(argv[++i], data_output_base)) continue;
            std::cerr << "无效的数据段基址: " << (i < argc ? argv[i] : "") << std::endl;
            return 1;
        }
        if (a.rfind("--data-base=", 0) == 0) {
            if (parse_u32(a.substr(12), data_output_base)) continue;
            std::cerr << "无效的数据段基址: " << a << std::endl;
            return 1;
        }
        if (a.rfind("-Tdata=", 0) == 0) {
            if (parse_u32(a.substr(7), data_output_base)) continue;
            std::cerr << "无效的数据段基址: " << a << std::endl;
            return 1;
        }
        if (a == "-v" || a == "--verbose") { verbose = true; continue; }
        if (a == "--no-fallback") { no_fallback = true; continue; }
        if (a == "-h" || a == "--help") {
            std::cerr << "用法: " << argv[0] << " [选项] file.o ..." << std::endl;
            std::cerr << "  -o <base>            输出基名，生成 <base>_code.bin / <base>_data.bin" << std::endl;
            std::cerr << "  -t <file>            输出链接后的符号表" << std::endl;
            std::cerr << "  -Ttext <addr>        设置代码段运行时基址（默认 0）" << std::endl;
            std::cerr << "  -Tdata <addr>        设置数据段运行时基址（默认 0）" << std::endl;
            std::cerr << "  -v, --verbose        显示详细链接信息" << std::endl;
            std::cerr << "  --no-fallback        禁用旧格式扫描回退，必须使用 ELF 重定位表" << std::endl;
            return 0;
        }
        inputs.push_back(a);
    }
    if (inputs.empty()) {
        std::cerr << "没有输入 .o 文件" << std::endl;
        return 1;
    }
    if (out_base.empty()) out_base = base_name(inputs[0]);

    std::vector<Object> objs;
    objs.reserve(inputs.size());
    for (const auto& in : inputs) {
        Object obj;
        if (!read_elf(in, obj)) {
            std::cerr << "无法读取或解析 ELF 文件: " << in << std::endl;
            return 1;
        }
        objs.push_back(std::move(obj));
    }

    // 第一遍：计算每个 .o 的 text/data 基址。
    std::vector<uint32_t> text_base(objs.size(), 0);
    std::vector<uint32_t> data_base(objs.size(), 0);
    size_t text_size_so_far = 0;
    size_t data_size_so_far = 0;
    for (size_t i = 0; i < objs.size(); i++) {
        text_base[i] = (uint32_t)text_size_so_far;
        data_base[i] = (uint32_t)data_size_so_far;
        uint32_t obj_text_cur = 0;
        uint32_t obj_data_cur = 0;
        for (size_t si = 0; si < objs[i].sections.size(); si++) {
            const Section& sec = objs[i].sections[si];
            if (section_is_text(sec)) {
                objs[i].section_offsets[si] = obj_text_cur;
                obj_text_cur += sec.size;
            } else if (section_is_data(sec)) {
                objs[i].section_offsets[si] = obj_data_cur;
                obj_data_cur += sec.size;
            }
        }
        text_size_so_far += obj_text_cur;
        data_size_so_far += obj_data_cur;
    }

    // 收集全局符号定义。
    std::map<std::string, ResolvedSymbol> globals;
    for (size_t i = 0; i < objs.size(); i++) {
        for (const auto& sym : objs[i].symbols) {
            if (sym.name.empty() || sym.is_undef() || !sym.is_global()) continue;
            if (sym.shndx == SHN_ABS) {
                auto ait = globals.find(sym.name);
                if (ait != globals.end()) {
                    std::cerr << "重复定义符号: " << sym.name << std::endl;
                    return 1;
                }
                globals[sym.name] = {sym.name, sym.value, true};
                continue;
            }
            if (sym.shndx >= objs[i].sections.size()) {
                std::cerr << "符号 " << sym.name << " 的段索引越界" << std::endl;
                return 1;
            }
            const Section& sec = objs[i].sections[sym.shndx];
            uint32_t addr = 0;
            if (section_is_text(sec))
                addr = text_output_base + text_base[i] + objs[i].section_offsets[sym.shndx] + sym.value;
            else if (section_is_data(sec))
                addr = data_output_base + data_base[i] + objs[i].section_offsets[sym.shndx] + sym.value;
            else continue; // 未输出段：未引用则忽略，引用时由 apply_one_relocation 报错
            auto it = globals.find(sym.name);
            if (it != globals.end()) {
                std::cerr << "重复定义符号: " << sym.name << std::endl;
                return 1;
            }
            globals[sym.name] = {sym.name, addr, true};
        }
    }

    // 第二遍：拼接段并应用重定位。
    std::vector<uint8_t> code, data;
    for (size_t i = 0; i < objs.size(); i++) {
        const Object& obj = objs[i];
        for (const auto& sec : obj.sections) {
            if (section_is_text(sec)) {
                code.insert(code.end(), sec.data.begin(), sec.data.end());
            } else if (section_is_data(sec)) {
                data.insert(data.end(), sec.data.begin(), sec.data.end());
                if (sec.type != SHT_PROGBITS) {
                    // .bss 等 NOBITS 段没有文件内容，按零填充输出。
                    if (sec.size > sec.data.size())
                        data.resize(data.size() + (sec.size - sec.data.size()), 0);
                }
            }
        }

        if (!obj.relocations.empty()) {
            for (const auto& rel : obj.relocations) {
                std::string err;
                if (apply_one_relocation(rel, obj, code, data, text_base, data_base,
                                          text_output_base, data_output_base, i, globals, err)) {
                    std::cerr << inputs[i] << ": " << err << std::endl;
                    return 1;
                }
            }
        } else if (no_fallback) {
            std::cerr << inputs[i] << ": 没有重定位表（--no-fallback 模式下拒绝旧格式）" << std::endl;
            return 1;
        } else {
            // 兼容没有重定位表的旧格式。
            patch_relocations_fallback(code, data, obj, text_base[i], data_base[i],
                                         text_output_base, data_output_base);
        }
    }

    std::string code_file = out_base + "_code.bin";
    std::string data_file = out_base + "_data.bin";
    if (!write_file(code_file, code)) { std::cerr << "无法写入 " << code_file << std::endl; return 1; }
    if (!write_file(data_file, data)) { std::cerr << "无法写入 " << data_file << std::endl; return 1; }

    if (verbose) {
        std::cerr << "text base = 0x" << std::hex << text_output_base << std::dec
                  << ", size = " << code.size() << std::endl;
        std::cerr << "data base = 0x" << std::hex << data_output_base << std::dec
                  << ", size = " << data.size() << std::endl;
        size_t reloc_total = 0;
        for (const auto& obj : objs) reloc_total += obj.relocations.size();
        std::cerr << "relocations = " << reloc_total << std::endl;
    }

    if (!table_file.empty()) {
        std::ofstream tf(table_file);
        if (tf) {
            tf << "TEXT size: " << code.size() << "\n";
            tf << "DATA size: " << data.size() << "\n";
            for (size_t i = 0; i < inputs.size(); i++)
                tf << "input: " << inputs[i] << "\n";
            tf << "symbols:\n";
            for (const auto& kv : globals) {
                if (kv.second.defined)
                    tf << kv.first << " = 0x" << std::hex << kv.second.address << std::dec << "\n";
            }
        }
    }

    std::cout << "链接成功: " << code_file << " (" << code.size() << " bytes), "
              << data_file << " (" << data.size() << " bytes)" << std::endl;
    return 0;
}
