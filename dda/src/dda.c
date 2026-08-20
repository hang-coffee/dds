/*
 * dda.c - DOCTOR ELF32 relocatable -> DASM 反汇编器
 *
 * 用法: dda <input.o> [output.asm]
 * 用 C99 编写。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#define ARRAY_LEN(a) (sizeof(a)/sizeof((a)[0]))

/* ---------------- ELF32 读取 ---------------- */

typedef struct {
    char name[64];
    uint32_t type;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    const uint8_t *data;
} Section;

typedef struct {
    char name[128];
    uint32_t value;
    uint16_t shndx;
} Symbol;

typedef struct {
    uint8_t *buf;
    size_t len;
    Section secs[64];
    int sec_count;
    Symbol syms[1024];
    int sym_count;
    int text_sec;
    int data_sec;
} ElfFile;

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int load_file(const char *path, uint8_t **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    rewind(f);
    uint8_t *p = (uint8_t*)malloc((size_t)sz ? (size_t)sz : 1);
    if (!p) { fclose(f); return -1; }
    if (fread(p, 1, (size_t)sz, f) != (size_t)sz) { free(p); fclose(f); return -1; }
    fclose(f);
    *out = p;
    *out_len = (size_t)sz;
    return 0;
}

static int parse_elf(ElfFile *elf, const char *path) {
    memset(elf, 0, sizeof(*elf));
    if (load_file(path, &elf->buf, &elf->len) != 0) return -1;
    const uint8_t *b = elf->buf;
    if (elf->len < 52 || b[0] != 0x7f || b[1] != 'E' || b[2] != 'L' || b[3] != 'F')
        return -1;
    uint32_t shoff = rd32(b + 32);
    uint16_t shentsize = rd16(b + 46);
    uint16_t shnum = rd16(b + 48);
    uint16_t shstrndx = rd16(b + 50);
    if (shentsize < 40 || shoff + (size_t)shentsize * shnum > elf->len)
        return -1;

    /* 段名表 */
    const uint8_t *shstr = NULL;
    size_t shstr_len = 0;
    if (shstrndx < shnum) {
        const uint8_t *sh = b + shoff + (size_t)shstrndx * shentsize;
        uint32_t off = rd32(sh + 16), sz = rd32(sh + 20);
        if (off + sz <= elf->len) { shstr = b + off; shstr_len = sz; }
    }

    elf->text_sec = -1;
    elf->data_sec = -1;
    for (int i = 0; i < shnum && elf->sec_count < 64; i++) {
        const uint8_t *sh = b + shoff + (size_t)i * shentsize;
        Section *s = &elf->secs[elf->sec_count];
        uint32_t name_off = rd32(sh + 0);
        s->type = rd32(sh + 4);
        s->offset = rd32(sh + 16);
        s->size = rd32(sh + 20);
        s->link = rd32(sh + 24);
        s->info = rd32(sh + 28);
        if (name_off < shstr_len) {
            snprintf(s->name, sizeof(s->name), "%s", (const char*)(shstr + name_off));
        } else {
            s->name[0] = 0;
        }
        if (s->offset + s->size <= elf->len)
            s->data = b + s->offset;
        else
            s->data = b;
        if (strcmp(s->name, ".text") == 0) elf->text_sec = elf->sec_count;
        if (strcmp(s->name, ".data") == 0 ||
            strcmp(s->name, ".rodata") == 0 ||
            strcmp(s->name, ".bss") == 0) {
            if (elf->data_sec < 0) elf->data_sec = elf->sec_count;
        }
        elf->sec_count++;
    }

    /* 符号表 */
    const uint8_t *strtab = NULL;
    size_t strtab_len = 0;
    for (int i = 0; i < elf->sec_count; i++) {
        if (elf->secs[i].type == 3 && strcmp(elf->secs[i].name, ".strtab") == 0) {
            strtab = elf->secs[i].data;
            strtab_len = elf->secs[i].size;
        }
    }
    for (int i = 0; i < elf->sec_count && elf->sym_count < 1024; i++) {
        const Section *s = &elf->secs[i];
        if (s->type != 2 || s->size < 16) continue;
        for (uint32_t off = 0; off + 16 <= s->size; off += 16) {
            const uint8_t *e = s->data + off;
            uint32_t st_name = rd32(e);
            uint32_t st_value = rd32(e + 4);
            uint16_t st_shndx = rd16(e + 14);
            if (st_name == 0) continue;
            Symbol *sym = &elf->syms[elf->sym_count];
            if (st_name < strtab_len)
                snprintf(sym->name, sizeof(sym->name), "%s", (const char*)(strtab + st_name));
            else
                sym->name[0] = 0;
            sym->value = st_value;
            sym->shndx = st_shndx;
            elf->sym_count++;
        }
    }
    return 0;
}

/* ---------------- 指令解码 ---------------- */

typedef struct {
    int opcode;
    int op_size;
    int has_nz;
    int has_rep;
    int op1;
    int op2;
    uint32_t imm;
    uint32_t imm_hi;
    int sr_k;
    int len;
} Instr;

static const char *mnemonics[] = {
    "let", "mov", "xchg", "lr", "st", "zero",
    "add", "sub", "mul", "div", "div qword", "csi", "cdi",
    "shl", "shr", "msl", "msr", "and", "or", "xor", "neg", "mne",
    "push", "pop", "sfa", "rer",
    "pushr", "popr", "sra", "srb", "lod", "sto", "sr",
    "test", "cmp", "jmp", "jz", "jnz", "jrz", "jrnz", "ja", "jna",
    "jb", "jnb", "jg", "jng", "jl", "jnl",
    "in", "out", "int", "push rin1", "push rin2", "pop rin1", "pop rin2",
    "pushi", "popi", "hlt",
    "blks", "pushp", "nop", "inc", "dec", "blkin",
    "svc", "iret", "setb", "getb", "por",
    "fmov", "fldi", "fld", "fst", "fadd", "fsub", "fmul", "fdiv",
    "fsqrt", "fneg", "fabs", "fcmp", "f2i", "i2f", "fpush", "fpop",
    "dmov", "dldi", "dld", "dst", "dadd", "dsub", "dmul", "ddiv",
    "dsqrt", "dneg", "dabs", "dcmp", "d2i", "i2d", "dpush", "dpop",
    "f2d", "d2f"
};

static int is_no_operand(int op) {
    switch (op) {
        case 10: /* div qword */
        case 11: case 12: /* csi cdi */
        case 25: /* rer */
        case 26: case 27: case 28: case 29: /* pushr popr sra srb */
        case 35: case 36: case 37: /* jmp jz jnz */
        case 51: case 52: case 53: case 54: /* push/pop rin */
        case 55: case 56: case 57: /* pushi popi hlt */
        case 59: /* pushp */
        case 60: /* nop */
        case 64: case 65: /* svc iret */
            return 1;
        default:
            return 0;
    }
}

static int is_single(int op) {
    switch (op) {
        case 5:  /* zero */
        case 20: /* neg */
        case 21: /* mne */
        case 22: case 23: /* push pop */
        case 24: /* sfa */
        case 30: case 31: /* lod sto */
        case 33: case 34: /* test cmp */
        case 38: case 39: case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 47: /* jrz..jnl */
        case 50: /* int */
        case 58: /* blks */
        case 61: case 62: /* inc dec */
        case 63: /* blkin */
        case 68: /* por */
        case 77: case 78: case 79: /* fsqrt fneg fabs */
        case 83: case 84: /* fpush fpop */
        case 93: case 94: case 95: /* dsqrt dneg dabs */
        case 99: case 100: /* dpush dpop */
            return 1;
        default:
            return 0;
    }
}

static int requires_size(int op) {
    switch (op) {
        case 0: case 3: case 4: case 6: case 7: case 8: case 9:
        case 13: case 14: case 15: case 16: case 17: case 18: case 19:
        case 21: case 22: case 23: case 24: case 30: case 31: case 32:
        case 33: case 34: case 38: case 39: case 40: case 41: case 42:
        case 43: case 44: case 45: case 46: case 47:
        case 48: case 49: case 58: case 59: case 63: case 68:
            return 1;
        default:
            return 0;
    }
}

static int is_fp_op(int op) { return op >= 69 && op <= 84; }
static int is_dp_op(int op) { return op >= 85 && op <= 102; }

static int decode_instr(const uint8_t *code, size_t size, size_t pos, Instr *d) {
    if (pos + 2 > size) return -1;
    uint8_t b0 = code[pos];
    uint8_t b1 = code[pos + 1];
    d->has_rep = (b0 & 0x80) != 0;
    d->op_size = (b0 >> 5) & 3;
    d->has_nz = (b0 >> 4) & 1;
    uint8_t need = b0 & 0x0f;
    d->opcode = b1 & 0x7f;
    d->imm = 0;
    d->imm_hi = 0;
    d->sr_k = 0;
    d->op1 = 0xe;
    d->op2 = 0xe;

    size_t off = pos + 2;
    if (need == 0) {
        d->len = 2;
        return 2;
    }
    if (off + 1 > size) return -1;
    uint8_t opbyte = code[off++];
    d->op1 = opbyte >> 4;
    d->op2 = opbyte & 0x0f;
    need--;

    if (need == 0) {
        d->len = (int)(off - pos);
        return d->len;
    }
    if (d->op_size < 1 || d->op_size > 3) return -1;

    /* LR/ST 的 *reg+N 偏移立即数 */
    if (d->opcode == 3 || d->opcode == 4) {
        uint32_t imm_bytes = 1u << (d->op_size - 1);
        if (need != imm_bytes || off + imm_bytes > size) return -1;
        uint32_t num = 0;
        for (uint32_t i = 0; i < imm_bytes; i++) num |= (uint32_t)code[off + i] << (i * 8);
        if (d->op_size == 1 && (num & 0x80)) num |= 0xffffff00u;
        else if (d->op_size == 2 && (num & 0x8000)) num |= 0xffff0000u;
        d->imm = num;
        d->len = (int)(off + imm_bytes - pos);
        return d->len;
    }

    if (off + 1 > size) return -1;
    uint8_t next = code[off++];
    if (d->opcode == 32) { /* SR */
        d->sr_k = next;
        need--;
        d->op1 = d->op1; /* base in op1 */
        if (need == 0) {
            d->len = (int)(off - pos);
            return d->len;
        }
    } else {
        off--; /* 不是 SR 时 next 属于立即数首字节 */
    }

    int imm_bytes = 1 << (d->op_size - 1);
    if (off + (size_t)imm_bytes > size) return -1;
    uint32_t num = 0;
    for (int i = 0; i < imm_bytes; i++) num |= (uint32_t)code[off + i] << (i * 8);
    d->imm = num;
    off += imm_bytes;
    if (d->opcode == 86) { /* DLDI: 再读高 32 位 */
        if (off + 4 > size) return -1;
        uint32_t hi = 0;
        for (int i = 0; i < 4; i++) hi |= (uint32_t)code[off + i] << (i * 8);
        d->imm_hi = hi;
        off += 4;
    }
    d->len = (int)(off - pos);
    return d->len;
}

/* ---------------- 输出格式化 ---------------- */

static const char *reg_names[12] = {
    "a", "b", "c", "d1", "d2", "s", "t", "f", "e", "r", "x", "i"
};
static const char *sysreg_names[9] = {
    "cbase", "climit", "dbase", "dlimit", "ksp", "rin3_ctrl", "xar", "ictb", "fpcr"
};

static const char *size_name(int s) {
    switch (s) {
        case 1: return "byte";
        case 2: return "word";
        case 3: return "dword";
        default: return "";
    }
}

static void append(char *buf, size_t cap, const char *s) {
    size_t n = strlen(buf);
    snprintf(buf + n, cap - n, "%s", s);
}

static void appendf(char *buf, size_t cap, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    append(buf, cap, tmp);
}

static void append_offset(char *out, size_t cap, uint32_t imm) {
    int32_t v = (int32_t)imm;
    if (v < 0) {
        unsigned mag = (unsigned)(-(int64_t)v);
        appendf(out, cap, "-%u", mag);
    } else if (v > 0) {
        appendf(out, cap, "+%u", v);
    }
}

static void format_operand(const Instr *d, int op, int deref, int pos, char *out, size_t cap) {
    out[0] = 0;
    if (op == 0xf) {
        if (d->opcode == 86) /* DLDI */
            snprintf(out, cap, "0x%08X%08X", d->imm_hi, d->imm);
        else
            snprintf(out, cap, "0x%X", d->imm);
        return;
    }
    if (op == 0xe) return;
    const char *name = "?";
    if (deref) {
        if (op < 12) name = reg_names[op];
    } else if (is_fp_op(d->opcode)) {
        int is_gpr = (d->opcode == 81 && pos == 1) || /* F2I: DR */
                     (d->opcode == 82 && pos == 2);   /* I2F: DR */
        if (!is_gpr && op <= 7) {
            static char tmp[16];
            snprintf(tmp, sizeof(tmp), "fp%d", op);
            name = tmp;
        } else if (op < 12) {
            name = reg_names[op];
        }
    } else if (is_dp_op(d->opcode)) {
        int use_fp = 0, use_dp = 0;
        if (d->opcode == 101) { /* F2D: dp, fp */
            use_dp = (pos == 1);
            use_fp = (pos == 2);
        } else if (d->opcode == 102) { /* D2F: fp, dp */
            use_fp = (pos == 1);
            use_dp = (pos == 2);
        } else if (d->opcode == 97 && pos == 1) { /* D2I: DR */
            /* general */
        } else if (d->opcode == 98 && pos == 2) { /* I2D: DR */
            /* general */
        } else {
            use_dp = 1;
        }
        if (use_fp && op <= 7) {
            static char tmp[16];
            snprintf(tmp, sizeof(tmp), "fp%d", op);
            name = tmp;
        } else if (use_dp && op <= 7) {
            static char tmp[16];
            snprintf(tmp, sizeof(tmp), "dp%d", op);
            name = tmp;
        } else if (op < 12) {
            name = reg_names[op];
        }
    } else if (op < 12) {
        name = reg_names[op];
    }
    if (deref)
        snprintf(out, cap, "*%s", name);
    else
        snprintf(out, cap, "%s", name);
}

static void format_instr(const Instr *d, char *out, size_t cap) {
    out[0] = 0;
    const char *mn = (d->opcode >= 0 && d->opcode < (int)ARRAY_LEN(mnemonics))
                     ? mnemonics[d->opcode] : "???";
    append(out, cap, mn);

    /* 需要尺寸的指令才输出尺寸；DFE/DDE 不输出 */
    if (requires_size(d->opcode) && d->op_size >= 1 && d->op_size <= 3)
        appendf(out, cap, " %s", size_name(d->op_size));

    if (is_no_operand(d->opcode)) {
        if (d->opcode == 59) snprintf(out, cap, "pushp"); /* pushp 无操作数 */
        return;
    }

    if (d->opcode == 32) { /* SR */
        char base[32], idx[32], off[32];
        format_operand(d, d->op1, 0, 1, base, sizeof(base));
        format_operand(d, d->op2, 0, 2, idx, sizeof(idx));
        snprintf(off, sizeof(off), "0x%X", d->imm);
        appendf(out, cap, " %s", base);
        if (idx[0]) {
            appendf(out, cap, "+%s*%d", idx, 1 << d->sr_k);
        }
        appendf(out, cap, "+%s", off);
        return;
    }

    if (is_single(d->opcode)) {
        int deref = 0;
        if (d->opcode == 68) deref = 1; /* POR */
        char op1[32];
        format_operand(d, d->op1, deref, 1, op1, sizeof(op1));
        if (op1[0]) appendf(out, cap, " %s", op1);
        return;
    }

    /* 双操作数 */
    int deref1 = 0, deref2 = 0;
    if (d->opcode == 4 || d->opcode == 72 || d->opcode == 88) deref1 = 1; /* ST/FST/DST */
    if (d->opcode == 3 || d->opcode == 71 || d->opcode == 87) deref2 = 1; /* LR/FLD/DLD */

    char op1[32], op2[32];
    format_operand(d, d->op1, deref1, 1, op1, sizeof(op1));
    format_operand(d, d->op2, deref2, 2, op2, sizeof(op2));

    if ((d->opcode == 3 || d->opcode == 4) && d->imm != 0) {
        if (deref1 && op1[0]) append_offset(op1, sizeof(op1), d->imm);
        if (deref2 && op2[0]) append_offset(op2, sizeof(op2), d->imm);
    }

    if (d->opcode == 66) { /* SETB: op1 sysreg, op2 DR */
        if (d->op1 <= 8) snprintf(op1, sizeof(op1), "%s", sysreg_names[d->op1]);
    }
    if (d->opcode == 67) { /* GETB: op1 DR, op2 sysreg */
        if (d->op2 <= 8) snprintf(op2, sizeof(op2), "%s", sysreg_names[d->op2]);
    }

    if (op1[0]) appendf(out, cap, " %s", op1);
    if (op2[0]) appendf(out, cap, ", %s", op2);
}

/* ---------------- 主程序 ---------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <input.o> [output.asm]\n", argv[0]);
        return 1;
    }
    ElfFile elf;
    if (parse_elf(&elf, argv[1]) != 0) {
        fprintf(stderr, "无法解析 ELF 文件: %s\n", argv[1]);
        return 1;
    }

    FILE *out = stdout;
    if (argc >= 3) {
        out = fopen(argv[2], "w");
        if (!out) { perror(argv[2]); return 1; }
    }

    /* 输出 DATA 段 */
    fprintf(out, "\tSECTION DATA\n\tORG 0\n");
    if (elf.data_sec >= 0) {
        const Section *ds = &elf.secs[elf.data_sec];
        for (int i = 0; i < elf.sym_count; i++) {
            if (elf.syms[i].shndx == (uint16_t)elf.data_sec && elf.syms[i].name[0])
                fprintf(out, "%s:\n", elf.syms[i].name);
        }
        if (ds->type == 8) { /* SHT_NOBITS (.bss) */
            fprintf(out, "\tRESB %u\n", ds->size);
        } else {
            for (uint32_t i = 0; i < ds->size; i++) {
                fprintf(out, "\tDB %u, 0x%02X\n", i, ds->data[i]);
            }
        }
    }

    /* 输出 TEXT 段 */
    fprintf(out, "\tSECTION TEXT\n\tORG 0\n");
    if (elf.text_sec >= 0) {
        const Section *ts = &elf.secs[elf.text_sec];
        size_t pos = 0;
        while (pos < ts->size) {
            for (int i = 0; i < elf.sym_count; i++) {
                if (elf.syms[i].shndx == (uint16_t)elf.text_sec &&
                    elf.syms[i].value == pos && elf.syms[i].name[0])
                    fprintf(out, "%s:\n", elf.syms[i].name);
            }
            Instr d;
            int len = decode_instr(ts->data, ts->size, pos, &d);
            if (len <= 0) {
                fprintf(out, "\t; invalid at 0x%zX\n", pos);
                pos++;
                continue;
            }
            char line[512];
            format_instr(&d, line, sizeof(line));
            fprintf(out, "\t%s\n", line);
            pos += (size_t)len;
        }
    }

    if (out != stdout) fclose(out);
    free(elf.buf);
    return 0;
}
