/* dlinker.c - DOCTOR 最小链接器（C99 重写）
 * 读取 dasm -m elf 生成的 ELF32 relocatable .o，合并 .text/.data 输出 code/data 二进制。
 * 支持 ELF 标准 .rel.text / .rel.data / .rela.text / .rela.data 重定位表，
 * 以及兼容旧的“扫描 32 位绝对值”回退逻辑。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_REL      9
#define SHN_UNDEF    0
#define SHN_ABS      0xFFF1

typedef struct {
    uint8_t *data;
    size_t size;
    size_t cap;
} ByteBuffer;

typedef struct {
    char *name;
    uint32_t type;
    uint32_t flags;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addralign;
    uint32_t entsize;
    ByteBuffer data;
} Section;

typedef struct {
    char *name;
    uint32_t value;
    uint16_t shndx;
    uint8_t info; /* ELF32 st_info: 高4位 bind, 低4位 type */
} Symbol;

typedef struct {
    uint32_t offset;
    uint32_t info;   /* ELF32 r_info: sym<<8 | type */
    int32_t addend;  /* RELA 的显式 addend；REL 中不使用 */
    int has_explicit_addend;
    uint32_t target_section;
} Relocation;

typedef struct {
    Section *sections;
    size_t nsections;
    size_t cap_sections;
    Symbol *symbols;
    size_t nsymbols;
    size_t cap_symbols;
    Relocation *relocations;
    size_t nrelocs;
    size_t cap_relocs;
    uint32_t *section_offsets; /* 输出段在对象内 text/data 区中的偏移 */
    size_t text_size;
    size_t data_size;
} Object;

typedef struct {
    char *name;
    uint32_t address;
    int defined;
} ResolvedSymbol;

typedef struct {
    ResolvedSymbol *items;
    size_t count;
    size_t cap;
} GlobalTable;

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "dlinker: 内存不足\n");
        exit(1);
    }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "dlinker: 内存不足\n");
        exit(1);
    }
    return q;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)xmalloc(n);
    memcpy(p, s, n);
    return p;
}

static char *xstrndup(const char *s, size_t n) {
    char *p = (char *)xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static void bb_init(ByteBuffer *b) {
    memset(b, 0, sizeof(*b));
}

static void bb_free(ByteBuffer *b) {
    free(b->data);
    memset(b, 0, sizeof(*b));
}

static void bb_reserve(ByteBuffer *b, size_t extra) {
    if (b->size + extra <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 64;
    while (nc < b->size + extra) nc *= 2;
    b->data = (uint8_t *)xrealloc(b->data, nc);
    b->cap = nc;
}

static void bb_append(ByteBuffer *b, const uint8_t *src, size_t n) {
    bb_reserve(b, n);
    if (n) memcpy(b->data + b->size, src, n);
    b->size += n;
}

static void bb_resize(ByteBuffer *b, size_t n) {
    if (n > b->cap) {
        size_t nc = b->cap ? b->cap : 64;
        while (nc < n) nc *= 2;
        b->data = (uint8_t *)xrealloc(b->data, nc);
        b->cap = nc;
    }
    if (n > b->size) memset(b->data + b->size, 0, n - b->size);
    b->size = n;
}

static uint32_t rd32(const uint8_t *b, size_t off) {
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) |
           ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}

static uint16_t rd16(const uint8_t *b, size_t off) {
    return (uint16_t)((uint16_t)b[off] | ((uint16_t)b[off + 1] << 8));
}

static int read_file_bytes(const char *path, ByteBuffer *b) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return 0; }
    rewind(f);
    bb_resize(b, (size_t)sz);
    if (sz > 0 && fread(b->data, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static int section_is_text(const Section *sec) {
    return strcmp(sec->name, ".text") == 0 || strncmp(sec->name, ".text.", 6) == 0;
}

static int section_is_data(const Section *sec) {
    return strcmp(sec->name, ".data") == 0 || strncmp(sec->name, ".data.", 6) == 0 ||
           strcmp(sec->name, ".rodata") == 0 || strncmp(sec->name, ".rodata.", 8) == 0 ||
           strcmp(sec->name, ".bss") == 0 || strncmp(sec->name, ".bss.", 5) == 0;
}

static void object_add_section(Object *obj, const Section *sec) {
    if (obj->nsections >= obj->cap_sections) {
        obj->cap_sections = obj->cap_sections ? obj->cap_sections * 2 : 16;
        obj->sections = (Section *)xrealloc(obj->sections,
                                            obj->cap_sections * sizeof(Section));
    }
    obj->sections[obj->nsections++] = *sec;
}

static void object_add_symbol(Object *obj, const Symbol *sym) {
    if (obj->nsymbols >= obj->cap_symbols) {
        obj->cap_symbols = obj->cap_symbols ? obj->cap_symbols * 2 : 16;
        obj->symbols = (Symbol *)xrealloc(obj->symbols,
                                          obj->cap_symbols * sizeof(Symbol));
    }
    obj->symbols[obj->nsymbols++] = *sym;
}

static void object_add_relocation(Object *obj, const Relocation *rel) {
    if (obj->nrelocs >= obj->cap_relocs) {
        obj->cap_relocs = obj->cap_relocs ? obj->cap_relocs * 2 : 16;
        obj->relocations = (Relocation *)xrealloc(obj->relocations,
                                                  obj->cap_relocs * sizeof(Relocation));
    }
    obj->relocations[obj->nrelocs++] = *rel;
}

static int read_elf(const char *path, Object *obj) {
    ByteBuffer b;
    bb_init(&b);
    if (!read_file_bytes(path, &b))
        return 0;
    if (b.size < 52 || b.data[0] != 0x7f || b.data[1] != 'E' ||
        b.data[2] != 'L' || b.data[3] != 'F') {
        bb_free(&b);
        return 0;
    }

    uint32_t shoff = rd32(b.data, 32);
    uint16_t shentsize = rd16(b.data, 46);
    uint16_t shnum = rd16(b.data, 48);
    uint16_t shstrndx = rd16(b.data, 50);
    if (shentsize < 40 || shoff + (size_t)shentsize * shnum > b.size) {
        bb_free(&b);
        return 0;
    }

    ByteBuffer shstr;
    bb_init(&shstr);
    if (shstrndx < shnum) {
        size_t p = shoff + (size_t)shstrndx * shentsize;
        uint32_t off = rd32(b.data, p + 16);
        uint32_t sz = rd32(b.data, p + 20);
        if (off + sz <= b.size)
            bb_append(&shstr, b.data + off, sz);
    }

    for (uint16_t i = 0; i < shnum; i++) {
        size_t p = shoff + (size_t)i * shentsize;
        uint32_t name_off = rd32(b.data, p);
        uint32_t type = rd32(b.data, p + 4);
        uint32_t flags = rd32(b.data, p + 8);
        uint32_t offset = rd32(b.data, p + 16);
        uint32_t size = rd32(b.data, p + 20);
        uint32_t link = rd32(b.data, p + 24);
        uint32_t info = rd32(b.data, p + 28);
        uint32_t addralign = rd32(b.data, p + 32);
        uint32_t entsize = rd32(b.data, p + 36);

        Section sec;
        memset(&sec, 0, sizeof(sec));
        bb_init(&sec.data);
        sec.name = (name_off < shstr.size) ? xstrndup((const char *)shstr.data + name_off,
                                                      strlen((const char *)shstr.data + name_off))
                                           : xstrdup("");
        sec.type = type;
        sec.flags = flags;
        sec.offset = offset;
        sec.size = size;
        sec.link = link;
        sec.info = info;
        sec.addralign = addralign;
        sec.entsize = entsize;
        if (offset + size <= b.size)
            bb_append(&sec.data, b.data + offset, size);
        object_add_section(obj, &sec);
    }

    obj->section_offsets = (uint32_t *)xmalloc((obj->nsections ? obj->nsections : 1) * sizeof(uint32_t));
    for (size_t i = 0; i < obj->nsections; i++) obj->section_offsets[i] = 0;

    /* 解析符号表：保留索引 0（空符号），保证重定位 r_info 能直接索引。 */
    for (size_t si = 0; si < obj->nsections; si++) {
        const Section *sec = &obj->sections[si];
        if (sec->type != SHT_SYMTAB || sec->size < 16 || sec->data.size < sec->size)
            continue;

        ByteBuffer strtab;
        bb_init(&strtab);
        if (sec->link < obj->nsections) {
            const Section *strsec = &obj->sections[sec->link];
            if (strsec->type == SHT_STRTAB && strsec->data.size > 0)
                bb_append(&strtab, strsec->data.data, strsec->data.size);
        }
        if (strtab.size == 0) {
            for (size_t mi = 0; mi < obj->nsections; mi++) {
                const Section *maybe = &obj->sections[mi];
                if (maybe->type == SHT_STRTAB && strcmp(maybe->name, ".strtab") == 0 &&
                    maybe->data.size > 0) {
                    bb_append(&strtab, maybe->data.data, maybe->data.size);
                    break;
                }
            }
        }
        for (uint32_t off = 0; off + 16 <= sec->size; off += 16) {
            size_t q = (size_t)off;
            uint32_t st_name = rd32(sec->data.data, q);
            uint32_t st_value = rd32(sec->data.data, q + 4);
            uint8_t st_info = sec->data.data[q + 12];
            uint16_t st_shndx = rd16(sec->data.data, q + 14);

            Symbol sym;
            memset(&sym, 0, sizeof(sym));
            sym.name = (st_name < strtab.size)
                           ? xstrdup((const char *)strtab.data + st_name)
                           : xstrdup("");
            sym.value = st_value;
            sym.shndx = st_shndx;
            sym.info = st_info;
            object_add_symbol(obj, &sym);
        }
        bb_free(&strtab);
    }

    /* 解析重定位表。 */
    for (size_t si = 0; si < obj->nsections; si++) {
        const Section *sec = &obj->sections[si];
        int is_rel = (sec->type == SHT_REL);
        int is_rela = (sec->type == SHT_RELA);
        if (!is_rel && !is_rela) continue;
        uint32_t entsize = sec->entsize ? sec->entsize : (is_rel ? 8 : 12);
        if (entsize < 8 || sec->data.size < sec->size) continue;
        for (uint32_t off = 0; off + entsize <= sec->size; off += entsize) {
            Relocation rel;
            rel.offset = rd32(sec->data.data, off);
            rel.info = rd32(sec->data.data, off + 4);
            rel.has_explicit_addend = is_rela;
            rel.addend = is_rela ? (int32_t)rd32(sec->data.data, off + 8) : 0;
            rel.target_section = sec->info;
            object_add_relocation(obj, &rel);
        }
    }

    for (size_t si = 0; si < obj->nsections; si++) {
        const Section *sec = &obj->sections[si];
        if (section_is_text(sec)) obj->text_size += sec->size;
        else if (section_is_data(sec)) obj->data_size += sec->size;
    }

    bb_free(&shstr);
    bb_free(&b);
    return 1;
}

static int write_file(const char *path, const ByteBuffer *buf) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    if (buf->size > 0 && fwrite(buf->data, 1, buf->size, f) != buf->size) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static uint32_t read_reloc_field(const ByteBuffer *buf, size_t pos, unsigned width) {
    uint32_t v = 0;
    for (unsigned i = 0; i < width; i++) {
        if (pos + i >= buf->size) return 0;
        v |= (uint32_t)buf->data[pos + i] << (i * 8);
    }
    return v;
}

static void write_reloc_field(ByteBuffer *buf, size_t pos, uint32_t value, unsigned width) {
    for (unsigned i = 0; i < width; i++) {
        if (pos + i < buf->size)
            buf->data[pos + i] = (uint8_t)((value >> (i * 8)) & 0xff);
    }
}

static int apply_one_relocation(const Relocation *rel, const Object *obj,
                                ByteBuffer *code, ByteBuffer *data,
                                const uint32_t *text_base, const uint32_t *data_base,
                                uint32_t text_output_base, uint32_t data_output_base,
                                size_t obj_index, const GlobalTable *globals,
                                char *err, size_t err_size) {
    uint32_t r_sym = (rel->info >> 8) & 0x00FFFFFF;
    uint32_t r_type = rel->info & 0xFF;
    if (r_type == 0) return 0;

    if (rel->target_section >= obj->nsections) {
        snprintf(err, err_size, "重定位目标段索引越界");
        return 1;
    }
    const Section *target = &obj->sections[rel->target_section];
    if (!section_is_text(target) && !section_is_data(target))
        return 0;

    if (r_sym >= obj->nsymbols) {
        snprintf(err, err_size, "重定位引用了越界的符号表索引 %u", r_sym);
        return 1;
    }
    const Symbol *sym = &obj->symbols[r_sym];

    uint32_t sym_addr = 0;
    int have_sym = 0;
    if (sym->shndx == SHN_ABS) {
        sym_addr = sym->value;
        have_sym = 1;
    } else if (!(sym->shndx == SHN_UNDEF)) {
        if (sym->shndx < obj->nsections) {
            const Section *sec = &obj->sections[sym->shndx];
            if (section_is_text(sec)) {
                sym_addr = text_output_base + text_base[obj_index]
                         + obj->section_offsets[sym->shndx] + sym->value;
                have_sym = 1;
            } else if (section_is_data(sec)) {
                sym_addr = data_output_base + data_base[obj_index]
                         + obj->section_offsets[sym->shndx] + sym->value;
                have_sym = 1;
            } else {
                snprintf(err, err_size, "符号 %s 所在的段不受支持: %s", sym->name, sec->name);
                return 1;
            }
        } else {
            snprintf(err, err_size, "符号 %s 的段索引越界", sym->name);
            return 1;
        }
    } else {
        const ResolvedSymbol *found = NULL;
        for (size_t i = 0; i < globals->count; i++) {
            if (strcmp(globals->items[i].name, sym->name) == 0) {
                found = &globals->items[i];
                break;
            }
        }
        if (!found || !found->defined) {
            snprintf(err, err_size, "未定义符号: %s", sym->name);
            return 1;
        }
        sym_addr = found->address;
        have_sym = 1;
    }
    if (!have_sym) {
        snprintf(err, err_size, "无法解析符号: %s", sym->name);
        return 1;
    }

    size_t pos;
    ByteBuffer *buf;
    uint32_t base;
    uint32_t sec_off = obj->section_offsets[rel->target_section];
    if (section_is_text(target)) {
        pos = text_base[obj_index] + sec_off + rel->offset;
        buf = code;
        base = text_output_base + text_base[obj_index] + sec_off;
    } else {
        pos = data_base[obj_index] + sec_off + rel->offset;
        buf = data;
        base = data_output_base + data_base[obj_index] + sec_off;
    }

    unsigned width = 0;
    int pc_relative = 0;
    switch (r_type) {
        case 0:  return 0;
        case 1:  width = 4; break;                 /* R_386_32 */
        case 2:  width = 4; pc_relative = 1; break; /* R_386_PC32 */
        case 4:  width = 4; pc_relative = 1; break; /* R_386_PLT32 */
        case 20: width = 2; break;                 /* R_386_16 */
        case 21: width = 2; pc_relative = 1; break;
        case 22: width = 1; break;                 /* R_386_8 */
        case 23: width = 1; pc_relative = 1; break;
        default:
            snprintf(err, err_size, "不支持的重定位类型: %u", r_type);
            return 1;
    }

    if (pos + width > buf->size) {
        snprintf(err, err_size, "重定位位置越界");
        return 1;
    }

    uint32_t addend;
    if (rel->has_explicit_addend) {
        addend = (uint32_t)rel->addend;
    } else if (!(sym->shndx == SHN_UNDEF) && !(sym->shndx == SHN_ABS)) {
        /* 同一对象内已定义符号：字段中已包含对象内偏移 sym->value，
           sym_addr 也已经包含 sym->value，因此 addend 只取“超出符号值的部分”
           （例如 label+4 时 =4，纯 label 时 =0），避免重复加。 */
        uint32_t field = read_reloc_field(buf, pos, width);
        addend = (field >= sym->value) ? (field - sym->value) : field;
    } else {
        addend = read_reloc_field(buf, pos, width);
    }
    uint32_t value = sym_addr + addend;
    if (pc_relative) {
        uint32_t place = base + rel->offset;
        value = sym_addr + addend - place;
    }
    write_reloc_field(buf, pos, value, width);
    return 0;
}

static void patch_relocations_fallback(ByteBuffer *text, ByteBuffer *data,
                                       const Object *obj,
                                       uint32_t text_base, uint32_t data_base,
                                       uint32_t text_output_base, uint32_t data_output_base) {
    size_t start_text = text_base;
    size_t size_text = obj->text_size;
    size_t end_text = start_text + size_text;
    if (end_text > text->size) end_text = text->size;

    for (size_t i = start_text; i + 4 <= end_text; i++) {
        uint32_t v = read_reloc_field(text, i, 4);
        for (size_t si = 0; si < obj->nsymbols; si++) {
            const Symbol *sym = &obj->symbols[si];
            if (sym->name[0] == 0 || sym->shndx == SHN_UNDEF) continue;
            if (sym->value == v) {
                uint32_t sec_off = (sym->shndx < obj->nsections)
                                       ? obj->section_offsets[sym->shndx] : 0;
                uint32_t base = (sym->shndx < obj->nsections &&
                                 section_is_data(&obj->sections[sym->shndx]))
                                    ? data_output_base + data_base + sec_off
                                    : text_output_base + text_base + sec_off;
                uint32_t nv = sym->value + base;
                write_reloc_field(text, i, nv, 4);
                break;
            }
        }
    }

    size_t start_data = data_base;
    size_t size_data = obj->data_size;
    size_t end_data = start_data + size_data;
    if (end_data > data->size) end_data = data->size;

    for (size_t i = start_data; i + 4 <= end_data; i++) {
        uint32_t v = read_reloc_field(data, i, 4);
        for (size_t si = 0; si < obj->nsymbols; si++) {
            const Symbol *sym = &obj->symbols[si];
            if (sym->name[0] == 0 || sym->shndx == SHN_UNDEF) continue;
            if (sym->value == v) {
                uint32_t sec_off = (sym->shndx < obj->nsections)
                                       ? obj->section_offsets[sym->shndx] : 0;
                uint32_t base = (sym->shndx < obj->nsections &&
                                 section_is_data(&obj->sections[sym->shndx]))
                                    ? data_output_base + data_base + sec_off
                                    : text_output_base + text_base + sec_off;
                uint32_t nv = sym->value + base;
                write_reloc_field(data, i, nv, 4);
                break;
            }
        }
    }
}

static char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t n = dot ? (size_t)(dot - base) : strlen(base);
    return xstrndup(base, n);
}

static int parse_u32(const char *s, uint32_t *out) {
    if (!s || !*s) return 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0);
    if (!end || *end != 0 || v > 0xFFFFFFFFUL) return 0;
    *out = (uint32_t)v;
    return 1;
}

static ResolvedSymbol *global_find(GlobalTable *t, const char *name) {
    for (size_t i = 0; i < t->count; i++) {
        if (strcmp(t->items[i].name, name) == 0) return &t->items[i];
    }
    return NULL;
}

static void global_add(GlobalTable *t, const char *name, uint32_t address) {
    if (t->count >= t->cap) {
        t->cap = t->cap ? t->cap * 2 : 16;
        t->items = (ResolvedSymbol *)xrealloc(t->items, t->cap * sizeof(ResolvedSymbol));
    }
    t->items[t->count].name = xstrdup(name);
    t->items[t->count].address = address;
    t->items[t->count].defined = 1;
    t->count++;
}

static void object_free(Object *obj) {
    for (size_t i = 0; i < obj->nsections; i++) {
        free(obj->sections[i].name);
        bb_free(&obj->sections[i].data);
    }
    free(obj->sections);
    for (size_t i = 0; i < obj->nsymbols; i++) free(obj->symbols[i].name);
    free(obj->symbols);
    free(obj->relocations);
    free(obj->section_offsets);
    memset(obj, 0, sizeof(*obj));
}

static void global_table_free(GlobalTable *t) {
    for (size_t i = 0; i < t->count; i++) free(t->items[i].name);
    free(t->items);
    memset(t, 0, sizeof(*t));
}

int main(int argc, char **argv) {
    const char *out_base = NULL;
    char *derived_out = NULL;
    const char *table_file = NULL;
    char **inputs = NULL;
    size_t ninputs = 0, capinputs = 0;
    uint32_t text_output_base = 0;
    uint32_t data_output_base = 0;
    int verbose = 0;
    int no_fallback = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-o") == 0) {
            if (i + 1 < argc) out_base = argv[++i];
            continue;
        }
        if (strcmp(a, "-t") == 0) {
            if (i + 1 < argc) table_file = argv[++i];
            continue;
        }
        if (strcmp(a, "-Ttext") == 0 || strcmp(a, "--text-base") == 0) {
            if (i + 1 < argc && parse_u32(argv[++i], &text_output_base)) continue;
            fprintf(stderr, "无效的代码段基址: %s\n", i < argc ? argv[i] : "");
            return 1;
        }
        if (strncmp(a, "--text-base=", 12) == 0) {
            if (parse_u32(a + 12, &text_output_base)) continue;
            fprintf(stderr, "无效的代码段基址: %s\n", a);
            return 1;
        }
        if (strncmp(a, "-Ttext=", 7) == 0) {
            if (parse_u32(a + 7, &text_output_base)) continue;
            fprintf(stderr, "无效的代码段基址: %s\n", a);
            return 1;
        }
        if (strcmp(a, "-Tdata") == 0 || strcmp(a, "--data-base") == 0) {
            if (i + 1 < argc && parse_u32(argv[++i], &data_output_base)) continue;
            fprintf(stderr, "无效的数据段基址: %s\n", i < argc ? argv[i] : "");
            return 1;
        }
        if (strncmp(a, "--data-base=", 12) == 0) {
            if (parse_u32(a + 12, &data_output_base)) continue;
            fprintf(stderr, "无效的数据段基址: %s\n", a);
            return 1;
        }
        if (strncmp(a, "-Tdata=", 7) == 0) {
            if (parse_u32(a + 7, &data_output_base)) continue;
            fprintf(stderr, "无效的数据段基址: %s\n", a);
            return 1;
        }
        if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            verbose = 1;
            continue;
        }
        if (strcmp(a, "--no-fallback") == 0) {
            no_fallback = 1;
            continue;
        }
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            fprintf(stderr, "用法: %s [选项] file.o ...\n", argv[0]);
            fprintf(stderr, "  -o <base>            输出基名，生成 <base>_code.bin / <base>_data.bin\n");
            fprintf(stderr, "  -t <file>            输出链接后的符号表\n");
            fprintf(stderr, "  -Ttext <addr>        设置代码段运行时基址（默认 0）\n");
            fprintf(stderr, "  -Tdata <addr>        设置数据段运行时基址（默认 0）\n");
            fprintf(stderr, "  -v, --verbose        显示详细链接信息\n");
            fprintf(stderr, "  --no-fallback        禁用旧格式扫描回退，必须使用 ELF 重定位表\n");
            return 0;
        }
        if (ninputs >= capinputs) {
            capinputs = capinputs ? capinputs * 2 : 16;
            inputs = (char **)xrealloc(inputs, capinputs * sizeof(char *));
        }
        inputs[ninputs++] = (char *)a;
    }

    if (ninputs == 0) {
        fprintf(stderr, "没有输入 .o 文件\n");
        return 1;
    }
    if (!out_base) {
        derived_out = base_name(inputs[0]);
        out_base = derived_out;
    }

    Object *objs = (Object *)xmalloc(ninputs * sizeof(Object));
    for (size_t i = 0; i < ninputs; i++) {
        memset(&objs[i], 0, sizeof(objs[i]));
        if (!read_elf(inputs[i], &objs[i])) {
            fprintf(stderr, "无法读取或解析 ELF 文件: %s\n", inputs[i]);
            for (size_t j = 0; j <= i; j++) object_free(&objs[j]);
            free(objs);
            free(derived_out);
            free(inputs);
            return 1;
        }
    }

    uint32_t *text_base = (uint32_t *)xmalloc(ninputs * sizeof(uint32_t));
    uint32_t *data_base = (uint32_t *)xmalloc(ninputs * sizeof(uint32_t));
    size_t text_size_so_far = 0;
    size_t data_size_so_far = 0;
    for (size_t i = 0; i < ninputs; i++) {
        text_base[i] = (uint32_t)text_size_so_far;
        data_base[i] = (uint32_t)data_size_so_far;
        uint32_t obj_text_cur = 0;
        uint32_t obj_data_cur = 0;
        for (size_t si = 0; si < objs[i].nsections; si++) {
            const Section *sec = &objs[i].sections[si];
            if (section_is_text(sec)) {
                objs[i].section_offsets[si] = obj_text_cur;
                obj_text_cur += sec->size;
            } else if (section_is_data(sec)) {
                objs[i].section_offsets[si] = obj_data_cur;
                obj_data_cur += sec->size;
            }
        }
        text_size_so_far += obj_text_cur;
        data_size_so_far += obj_data_cur;
    }

    GlobalTable globals;
    memset(&globals, 0, sizeof(globals));
    for (size_t i = 0; i < ninputs; i++) {
        for (size_t si = 0; si < objs[i].nsymbols; si++) {
            const Symbol *sym = &objs[i].symbols[si];
            int bind = (sym->info >> 4) & 0xf;
            int is_global = (bind == 1 || bind == 2);
            if (sym->name[0] == 0 || sym->shndx == SHN_UNDEF || !is_global) continue;
            if (sym->shndx == SHN_ABS) {
                if (global_find(&globals, sym->name)) {
                    fprintf(stderr, "重复定义符号: %s\n", sym->name);
                    return 1;
                }
                global_add(&globals, sym->name, sym->value);
                continue;
            }
            if (sym->shndx >= objs[i].nsections) {
                fprintf(stderr, "符号 %s 的段索引越界\n", sym->name);
                return 1;
            }
            const Section *sec = &objs[i].sections[sym->shndx];
            uint32_t addr = 0;
            if (section_is_text(sec))
                addr = text_output_base + text_base[i] + objs[i].section_offsets[sym->shndx] + sym->value;
            else if (section_is_data(sec))
                addr = data_output_base + data_base[i] + objs[i].section_offsets[sym->shndx] + sym->value;
            else continue;
            if (global_find(&globals, sym->name)) {
                fprintf(stderr, "重复定义符号: %s\n", sym->name);
                return 1;
            }
            global_add(&globals, sym->name, addr);
        }
    }

    ByteBuffer code, data;
    bb_init(&code);
    bb_init(&data);
    for (size_t i = 0; i < ninputs; i++) {
        const Object *obj = &objs[i];
        for (size_t si = 0; si < obj->nsections; si++) {
            const Section *sec = &obj->sections[si];
            if (section_is_text(sec)) {
                bb_append(&code, sec->data.data, sec->data.size);
            } else if (section_is_data(sec)) {
                bb_append(&data, sec->data.data, sec->data.size);
                if (sec->type != SHT_PROGBITS && sec->size > sec->data.size)
                    bb_resize(&data, data.size + (sec->size - sec->data.size));
            }
        }

        if (obj->nrelocs > 0) {
            for (size_t ri = 0; ri < obj->nrelocs; ri++) {
                char err[512];
                if (apply_one_relocation(&obj->relocations[ri], obj, &code, &data,
                                         text_base, data_base,
                                         text_output_base, data_output_base,
                                         i, &globals, err, sizeof(err))) {
                    fprintf(stderr, "%s: %s\n", inputs[i], err);
                    return 1;
                }
            }
        } else if (no_fallback) {
            fprintf(stderr, "%s: 没有重定位表（--no-fallback 模式下拒绝旧格式）\n", inputs[i]);
            return 1;
        } else {
            patch_relocations_fallback(&code, &data, obj, text_base[i], data_base[i],
                                       text_output_base, data_output_base);
        }
    }

    size_t out_base_len = strlen(out_base);
    char *code_file = (char *)xmalloc(out_base_len + 10);
    char *data_file = (char *)xmalloc(out_base_len + 10);
    sprintf(code_file, "%s_code.bin", out_base);
    sprintf(data_file, "%s_data.bin", out_base);

    if (!write_file(code_file, &code)) {
        fprintf(stderr, "无法写入 %s\n", code_file);
        return 1;
    }
    if (!write_file(data_file, &data)) {
        fprintf(stderr, "无法写入 %s\n", data_file);
        return 1;
    }

    if (verbose) {
        fprintf(stderr, "text base = 0x%x, size = %zu\n", text_output_base, code.size);
        fprintf(stderr, "data base = 0x%x, size = %zu\n", data_output_base, data.size);
        size_t reloc_total = 0;
        for (size_t i = 0; i < ninputs; i++) reloc_total += objs[i].nrelocs;
        fprintf(stderr, "relocations = %zu\n", reloc_total);
    }

    if (table_file) {
        FILE *tf = fopen(table_file, "w");
        if (tf) {
            fprintf(tf, "TEXT size: %zu\n", code.size);
            fprintf(tf, "DATA size: %zu\n", data.size);
            for (size_t i = 0; i < ninputs; i++)
                fprintf(tf, "input: %s\n", inputs[i]);
            fprintf(tf, "symbols:\n");
            for (size_t i = 0; i < globals.count; i++) {
                if (globals.items[i].defined)
                    fprintf(tf, "%s = 0x%x\n", globals.items[i].name, globals.items[i].address);
            }
            fclose(tf);
        }
    }

    printf("链接成功: %s (%zu bytes), %s (%zu bytes)\n",
           code_file, code.size, data_file, data.size);

    free(code_file);
    free(data_file);
    bb_free(&code);
    bb_free(&data);
    global_table_free(&globals);
    free(text_base);
    free(data_base);
    for (size_t i = 0; i < ninputs; i++) object_free(&objs[i]);
    free(objs);
    free(derived_out);
    free(inputs);
    return 0;
}
