#include "lexer.h"
#include <cctype>
#include <cstdlib>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <iostream>

namespace dasm {

static std::string to_lower(const std::string& s) {
    std::string res = s;
    for (char& c : res) c = std::tolower(static_cast<unsigned char>(c));
    return res;
}

static const std::unordered_map<std::string, token_type> instr_map = {
    {"let", TOK_INSTR_LET}, {"mov", TOK_INSTR_MOV}, {"xchg", TOK_INSTR_XCHG},
    {"lr", TOK_INSTR_LR}, {"st", TOK_INSTR_ST}, {"zero", TOK_INSTR_ZERO},
    {"add", TOK_INSTR_ADD}, {"sub", TOK_INSTR_SUB}, {"mul", TOK_INSTR_MUL},
    {"div", TOK_INSTR_DIV}, {"csi", TOK_INSTR_CSI}, {"cdi", TOK_INSTR_CDI},
    {"shl", TOK_INSTR_SHL}, {"shr", TOK_INSTR_SHR}, {"msl", TOK_INSTR_MSL},
    {"msr", TOK_INSTR_MSR}, {"and", TOK_INSTR_AND}, {"or", TOK_INSTR_OR},
    {"xor", TOK_INSTR_XOR}, {"neg", TOK_INSTR_NEG}, {"mne", TOK_INSTR_MNE},
    {"push", TOK_INSTR_PUSH}, {"pop", TOK_INSTR_POP}, {"sfa", TOK_INSTR_SFA},
    {"rer", TOK_INSTR_RER}, {"pushr", TOK_INSTR_PUSHR}, {"popr", TOK_INSTR_POPR},
    {"sra", TOK_INSTR_SRA}, {"srb", TOK_INSTR_SRB}, {"lod", TOK_INSTR_LOD},
    {"sto", TOK_INSTR_STO}, {"sr", TOK_INSTR_SR}, {"test", TOK_INSTR_TEST},
    {"cmp", TOK_INSTR_CMP}, {"jmp", TOK_INSTR_JMP}, {"jz", TOK_INSTR_JZ},
    {"jnz", TOK_INSTR_JNZ}, {"jrz", TOK_INSTR_JRZ}, {"jrnz", TOK_INSTR_JRNZ},
    {"ja", TOK_INSTR_JA}, {"jna", TOK_INSTR_JNA}, {"jb", TOK_INSTR_JB},
    {"jnb", TOK_INSTR_JNB}, {"jg", TOK_INSTR_JG}, {"jng", TOK_INSTR_JNG},
    {"jl", TOK_INSTR_JL}, {"jnl", TOK_INSTR_JNL}, {"in", TOK_INSTR_IN},
    {"out", TOK_INSTR_OUT}, {"int", TOK_INSTR_INT}, {"pushi", TOK_INSTR_PUSHI},
    {"popi", TOK_INSTR_POPI}, {"hlt", TOK_INSTR_HLT}, {"blks", TOK_INSTR_BLKS},
    {"pushp", TOK_INSTR_PUSH_P}, {"nop", TOK_INSTR_NOP}, {"inc", TOK_INSTR_INC},
    {"dec", TOK_INSTR_DEC}, {"blkin", TOK_INSTR_BLKIN}, {"svc", TOK_INSTR_SVC},
    {"iret", TOK_INSTR_IRET}, {"setb", TOK_INSTR_SETB}, {"getb", TOK_INSTR_GETB},
    {"por", TOK_INSTR_POR},
    {"fmov", TOK_INSTR_FMOV}, {"fldi", TOK_INSTR_FLDI},
    {"fld", TOK_INSTR_FLD}, {"fst", TOK_INSTR_FST},
    {"fadd", TOK_INSTR_FADD}, {"fsub", TOK_INSTR_FSUB},
    {"fmul", TOK_INSTR_FMUL}, {"fdiv", TOK_INSTR_FDIV},
    {"fsqrt", TOK_INSTR_FSQRT}, {"fneg", TOK_INSTR_FNEG},
    {"fabs", TOK_INSTR_FABS}, {"fcmp", TOK_INSTR_FCMP},
    {"f2i", TOK_INSTR_F2I}, {"i2f", TOK_INSTR_I2F},
    {"fpush", TOK_INSTR_FPUSH}, {"fpop", TOK_INSTR_FPOP},
    {"dmov", TOK_INSTR_DMOV}, {"dldi", TOK_INSTR_DLDI},
    {"dld", TOK_INSTR_DLD}, {"dst", TOK_INSTR_DST},
    {"dadd", TOK_INSTR_DADD}, {"dsub", TOK_INSTR_DSUB},
    {"dmul", TOK_INSTR_DMUL}, {"ddiv", TOK_INSTR_DDIV},
    {"dsqrt", TOK_INSTR_DSQRT}, {"dneg", TOK_INSTR_DNEG},
    {"dabs", TOK_INSTR_DABS}, {"dcmp", TOK_INSTR_DCMP},
    {"d2i", TOK_INSTR_D2I}, {"i2d", TOK_INSTR_I2D},
    {"dpush", TOK_INSTR_DPUSH}, {"dpop", TOK_INSTR_DPOP},
    {"f2d", TOK_INSTR_F2D}, {"d2f", TOK_INSTR_D2F},
    {"emov", TOK_INSTR_EMOV}, {"eldi", TOK_INSTR_ELDI},
    {"eld", TOK_INSTR_ELD}, {"est", TOK_INSTR_EST},
    {"eadd", TOK_INSTR_EADD}, {"esub", TOK_INSTR_ESUB},
    {"emul", TOK_INSTR_EMUL}, {"ediv", TOK_INSTR_EDIV},
    {"esqrt", TOK_INSTR_ESQRT}, {"eneg", TOK_INSTR_ENEG},
    {"eabs", TOK_INSTR_EABS}, {"ecmp", TOK_INSTR_ECMP},
    {"e2i", TOK_INSTR_E2I}, {"i2e", TOK_INSTR_I2E},
    {"f2e", TOK_INSTR_F2E}, {"e2f", TOK_INSTR_E2F},
    {"d2e", TOK_INSTR_D2E}, {"e2d", TOK_INSTR_E2D},
    {"epush", TOK_INSTR_EPUSH}, {"epop", TOK_INSTR_EPOP},
};

static const std::unordered_map<std::string, token_type> multi_instr_map = {
    {"div qword", TOK_INSTR_DIV_QWORD},
    {"push rin1", TOK_INSTR_PUSH_RIN1},
    {"push rin2", TOK_INSTR_PUSH_RIN2},
    {"pop rin1", TOK_INSTR_POP_RIN1},
    {"pop rin2", TOK_INSTR_POP_RIN2},
};

static const std::unordered_map<std::string, token_type> reg_map = {
    {"a", TOK_REG_A}, {"b", TOK_REG_B}, {"c", TOK_REG_C},
    {"d1", TOK_REG_D1}, {"d2", TOK_REG_D2}, {"e", TOK_REG_E},
    {"f", TOK_REG_F}, {"i", TOK_REG_I}, {"p", TOK_REG_P},
    {"r", TOK_REG_R}, {"s", TOK_REG_S}, {"t", TOK_REG_T},
    {"x", TOK_REG_X},
    {"fp0", TOK_REG_FP0}, {"fp1", TOK_REG_FP1},
    {"fp2", TOK_REG_FP2}, {"fp3", TOK_REG_FP3},
    {"fp4", TOK_REG_FP4}, {"fp5", TOK_REG_FP5},
    {"fp6", TOK_REG_FP6}, {"fp7", TOK_REG_FP7},
    {"dp0", TOK_REG_DP0}, {"dp1", TOK_REG_DP1},
    {"dp2", TOK_REG_DP2}, {"dp3", TOK_REG_DP3},
    {"dp4", TOK_REG_DP4}, {"dp5", TOK_REG_DP5},
    {"dp6", TOK_REG_DP6}, {"dp7", TOK_REG_DP7},
    {"ep0", TOK_REG_EP0}, {"ep1", TOK_REG_EP1},
    {"ep2", TOK_REG_EP2}, {"ep3", TOK_REG_EP3},
    {"ep4", TOK_REG_EP4}, {"ep5", TOK_REG_EP5},
    {"ep6", TOK_REG_EP6}, {"ep7", TOK_REG_EP7},
};

static const std::unordered_map<std::string, token_type> sysreg_map = {
    {"cbase", TOK_SYSREG_CBASE}, {"climit", TOK_SYSREG_CLIMIT},
    {"dbase", TOK_SYSREG_DBASE}, {"dlimit", TOK_SYSREG_DLIMIT},
    {"ksp", TOK_SYSREG_KSP}, {"rin3_ctrl", TOK_SYSREG_RIN3_CTRL},
    {"xar", TOK_SYSREG_XAR}, {"ictb", TOK_SYSREG_ICTB},
    {"fpcr", TOK_SYSREG_FPCR},
};

static const std::unordered_map<std::string, token_type> pseudo_map = {
    {"section", TOK_PSEUDO_SECTION}, {"org", TOK_PSEUDO_ORG},
    {"db", TOK_PSEUDO_DB}, {"dw", TOK_PSEUDO_DW}, {"dd", TOK_PSEUDO_DD},
    {"dq", TOK_PSEUDO_DQ}, {"resb", TOK_PSEUDO_RESB},
    {"extern", TOK_PSEUDO_EXTERN},
};

static const std::unordered_map<std::string, token_type> prefix_map = {
    {"rep", TOK_REP_PREFIX},
};

void lexer_init(lexer_context& ctx, const std::vector<std::pair<int, std::string>>& lines) {
    ctx.lines = lines;
    ctx.current_line = 0;
    ctx.current_pos = 0;
    ctx.line_no = 1;
    ctx.error.clear();
    ctx.has_pending = false;
}

static void skip_whitespace(lexer_context& ctx) {
    if (ctx.current_line >= ctx.lines.size()) return;
    const std::string& line = ctx.lines[ctx.current_line].second;
    while (ctx.current_pos < line.size() && std::isspace(static_cast<unsigned char>(line[ctx.current_pos]))) {
        ctx.current_pos++;
    }
}

static bool parse_number(const std::string& text, unsigned long long& value,
                          unsigned long long& value_hi) {
    std::string cleaned;
    for (char c : text) {
        if (c != '_') cleaned.push_back(c);
    }
    value = 0;
    value_hi = 0;

    // 检查二进制后缀 (b 或 B) —— 注意: 0x 开头的十六进制数以 b/B 结尾时不能被当作二进制
    if (!cleaned.empty() && (cleaned.back() == 'b' || cleaned.back() == 'B')
        && cleaned.compare(0, 2, "0x") != 0 && cleaned.compare(0, 2, "0X") != 0) {
        // 去掉后缀，手动转换二进制
        std::string bin_str = cleaned.substr(0, cleaned.size() - 1);
        if (bin_str.empty()) return false;
        unsigned long long val = 0;
        for (char c : bin_str) {
            if (c != '0' && c != '1') return false;
            val = (val << 1) | (c - '0');
        }
        value = val;
        return true;
    }

    // 十六进制 (0x 开头)，支持最多 80 位（20 个十六进制数字）
    if (cleaned.compare(0, 2, "0x") == 0 || cleaned.compare(0, 2, "0X") == 0) {
        const char* p = cleaned.c_str() + 2;
        if (*p == '\0') return false;
        unsigned long long lo = 0, hi = 0;
        int digits = 0;
        while (*p) {
            int d = 0;
            char c = *p;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return false;
            unsigned long long carry = (lo >> 60) & 0xFULL;
            lo = (lo << 4) | (unsigned long long)d;
            hi = (hi << 4) | carry;
            digits++;
            p++;
        }
        if (digits > 20) return false;   // 最多 80 位
        value = lo;
        value_hi = hi & 0xFFFFULL;       // 80 位立即数只需高 16 位
        return true;
    }

    // 十进制（默认）
    char* endptr = nullptr;
    value = std::strtoull(cleaned.c_str(), &endptr, 10);
    return (endptr != cleaned.c_str() && *endptr == '\0');
}

bool lexer_next_token(lexer_context& ctx, token& out_tok) {
    // 先吐出一个暂存的后续 token（NZ 拼接形式产生的第二个 token）
    if (ctx.has_pending) {
        out_tok = ctx.pending_tok;
        ctx.has_pending = false;
        return true;
    }

    if (ctx.lines.empty()) {
        out_tok.type = TOK_EOF;
        out_tok.text = "";
        return false;
    }

    while (ctx.current_line < ctx.lines.size()) {
        ctx.line_no = ctx.lines[ctx.current_line].first;   // 使用源文件真实行号
        const std::string& line = ctx.lines[ctx.current_line].second;
        skip_whitespace(ctx);
        if (ctx.current_pos < line.size()) break;
        ctx.current_line++;
        ctx.current_pos = 0;
    }

    if (ctx.current_line >= ctx.lines.size()) {
        out_tok.type = TOK_EOF;
        out_tok.text = "";
        return false;
    }

    const std::string& line = ctx.lines[ctx.current_line].second;
    out_tok.line_no = ctx.line_no;
    out_tok.col_no = ctx.current_pos;
    char ch = line[ctx.current_pos];
    size_t start = ctx.current_pos;

    // ---- 1. 标号定义 (label:) ----
    size_t colon_pos = line.find(':', start);
    if (colon_pos != std::string::npos && colon_pos > start) {
        std::string label_text = line.substr(start, colon_pos - start);
        bool valid = true;
        for (char c : label_text) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.') {
                valid = false;
                break;
            }
        }
        if (valid) {
            ctx.current_pos = colon_pos + 1;
            out_tok.type = TOK_LABEL_DEF;
            out_tok.text = label_text;
            out_tok.value = 0;
            out_tok.value_hi = 0;
            out_tok.bytes.clear();
            return true;
        }
    }

    // ---- 2. 字符串立即数 ('...' 或 "...") ----
    if (ch == '\'' || ch == '"') {
        char quote = ch;
        ctx.current_pos++;
        // 手动扫描结束引号（跳过 \ 转义符，支持 \" \' 等转义引号）
        size_t end = ctx.current_pos;
        while (end < line.size()) {
            if (line[end] == '\\') { end += 2; continue; }
            if (line[end] == quote) break;
            end++;
        }
        if (end >= line.size()) {
            ctx.error = "未闭合的字符串";
            out_tok.type = TOK_UNKNOWN;
            return false;
        }
        std::string str_val = line.substr(ctx.current_pos, end - ctx.current_pos);
        ctx.current_pos = end + 1;
        out_tok.bytes.clear();
        for (size_t i = 0; i < str_val.size(); ++i) {
            char c = str_val[i];
            if (c == '\\' && i + 1 < str_val.size()) {
                char n = str_val[i + 1];
                switch (n) {
                    case 'n': out_tok.bytes.push_back('\n'); break;
                    case 't': out_tok.bytes.push_back('\t'); break;
                    case 'r': out_tok.bytes.push_back('\r'); break;
                    case '0': out_tok.bytes.push_back('\0'); break;
                    case '\\': out_tok.bytes.push_back('\\'); break;
                    case '"': out_tok.bytes.push_back('"'); break;
                    case '\'': out_tok.bytes.push_back('\''); break;
                    default: out_tok.bytes.push_back(static_cast<unsigned char>(n)); break;
                }
                i++;
            } else {
                out_tok.bytes.push_back(static_cast<unsigned char>(c));
            }
        }
        out_tok.type = TOK_STRING;
        out_tok.text = str_val;
        out_tok.value = 0;
            out_tok.value_hi = 0;
        return true;
    }

    // ---- 3. 立即数（数字开头，含下划线和二进制后缀） ----
    if (std::isdigit(static_cast<unsigned char>(ch)) ||
        (ch == '.' && ctx.current_pos + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[ctx.current_pos + 1])))) {
        while (ctx.current_pos < line.size()) {
            char c = line[ctx.current_pos];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') {
                ctx.current_pos++;
            } else {
                break;
            }
        }
        std::string num_str = line.substr(start, ctx.current_pos - start);
        unsigned long long val, val_hi;
        if (!parse_number(num_str, val, val_hi)) {
            ctx.error = "无效数字: " + num_str;
            out_tok.type = TOK_UNKNOWN;
            return false;
        }
        out_tok.type = TOK_IMMEDIATE;
        out_tok.text = num_str;
        out_tok.value = val;
        out_tok.value_hi = val_hi;
        out_tok.bytes.clear();
        return true;
    }

    // ---- 4. 标识符 ----
    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
        while (ctx.current_pos < line.size()) {
            char c = line[ctx.current_pos];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') {
                ctx.current_pos++;
            } else {
                break;
            }
        }
        std::string word = line.substr(start, ctx.current_pos - start);
        size_t saved_pos = ctx.current_pos;
        size_t scan_pos = ctx.current_pos;
        std::string combined = to_lower(word);
        bool matched_multi = false;
        while (scan_pos < line.size()) {
            while (scan_pos < line.size() && std::isspace(static_cast<unsigned char>(line[scan_pos]))) scan_pos++;
            if (scan_pos >= line.size()) break;
            if (!std::isalpha(static_cast<unsigned char>(line[scan_pos])) && line[scan_pos] != '_') break;
            size_t word_start = scan_pos;
            while (scan_pos < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[scan_pos])) || line[scan_pos] == '_' || line[scan_pos] == '.')) scan_pos++;
            std::string next_word = line.substr(word_start, scan_pos - word_start);
            std::string candidate = combined + " " + to_lower(next_word);
            auto it = multi_instr_map.find(candidate);
            if (it != multi_instr_map.end()) {
                ctx.current_pos = scan_pos;
                out_tok.type = it->second;
                out_tok.text = line.substr(start, ctx.current_pos - start);
                out_tok.value = 0;
            out_tok.value_hi = 0;
                out_tok.bytes.clear();
                matched_multi = true;
                break;
            }
            combined = candidate;
        }
        if (matched_multi) return true;

        ctx.current_pos = saved_pos;
        std::string low = to_lower(word);

        // REP prefix
        auto prefix_it = prefix_map.find(low);
        if (prefix_it != prefix_map.end()) {
            out_tok.type = prefix_it->second;
            out_tok.text = word;
            out_tok.bytes.clear();
            return true;
        }

        // 尺寸说明
        if (low == "byte")   { out_tok.type = TOK_SIZE_BYTE;   out_tok.text = word; out_tok.bytes.clear(); return true; }
        if (low == "word")   { out_tok.type = TOK_SIZE_WORD;   out_tok.text = word; out_tok.bytes.clear(); return true; }
        if (low == "dword")  { out_tok.type = TOK_SIZE_DWORD;  out_tok.text = word; out_tok.bytes.clear(); return true; }
        if (low == "qword")  { out_tok.type = TOK_SIZE_QWORD;  out_tok.text = word; out_tok.bytes.clear(); return true; }
        if (low == "nz")     { out_tok.type = TOK_NZ_SUFFIX;   out_tok.text = word; out_tok.bytes.clear(); return true; }

        // 伪指令
        auto pseudo_it = pseudo_map.find(low);
        if (pseudo_it != pseudo_map.end()) {
            out_tok.type = pseudo_it->second;
            out_tok.text = word;
            out_tok.bytes.clear();
            return true;
        }

        // 系统寄存器
        auto sys_it = sysreg_map.find(low);
        if (sys_it != sysreg_map.end()) {
            out_tok.type = sys_it->second;
            out_tok.text = word;
            out_tok.bytes.clear();
            return true;
        }

        // 寄存器
        auto reg_it = reg_map.find(low);
        if (reg_it != reg_map.end()) {
            out_tok.type = reg_it->second;
            out_tok.text = word;
            out_tok.bytes.clear();
            return true;
        }

        // 指令
        auto instr_it = instr_map.find(low);
        if (instr_it != instr_map.end()) {
            out_tok.type = instr_it->second;
            out_tok.text = word;
            out_tok.bytes.clear();
            return true;
        }

        // NZ 拼接形式（如 ADDNZ / LETNZ / XORNZ）: 指令名 + "nz" 结尾
        // （完整指令名如 jnz/jrnz 已在上面 instr_map 命中，不会走到这里）
        if (low.size() > 2 && low.compare(low.size() - 2, 2, "nz") == 0) {
            std::string stem = low.substr(0, low.size() - 2);
            auto stem_it = instr_map.find(stem);
            if (stem_it != instr_map.end()) {
                out_tok.type = stem_it->second;
                out_tok.text = word.substr(0, word.size() - 2);
                out_tok.value = 0;
            out_tok.value_hi = 0;
                out_tok.bytes.clear();

                ctx.pending_tok.type = TOK_NZ_SUFFIX;
                ctx.pending_tok.text = word.substr(word.size() - 2);
                ctx.pending_tok.line_no = ctx.line_no;
                ctx.pending_tok.col_no = start + static_cast<size_t>(word.size() - 2);
                ctx.pending_tok.value = 0;
                ctx.pending_tok.value_hi = 0;
                ctx.pending_tok.bytes.clear();
                ctx.has_pending = true;
                return true;
            }
        }

        // 否则作为标号引用
        out_tok.type = TOK_LABEL_REF;
        out_tok.text = word;
        out_tok.value = 0;
            out_tok.value_hi = 0;
        out_tok.bytes.clear();
        return true;
    }

    // ---- 5. 特殊符号 ----
    ctx.current_pos++;
    switch (ch) {
        case '*': out_tok.type = TOK_STAR; break;
        case '+': out_tok.type = TOK_PLUS; break;
        case '-': out_tok.type = TOK_MINUS; break;
        case ',': out_tok.type = TOK_COMMA; break;
        case ':': out_tok.type = TOK_COLON; break;
        case '$': out_tok.type = TOK_DOLLAR; break;
        default:  out_tok.type = TOK_UNKNOWN; ctx.error = "未知字符: " + std::string(1, ch); break;
    }
    out_tok.text = std::string(1, ch);
    out_tok.value = 0;
            out_tok.value_hi = 0;
    out_tok.bytes.clear();
    return (out_tok.type != TOK_UNKNOWN);
}

bool lexer_peek_token(lexer_context& ctx, token& out_tok) {
    size_t saved_line = ctx.current_line;
    size_t saved_pos = ctx.current_pos;
    int saved_line_no = ctx.line_no;
    bool saved_pending = ctx.has_pending;
    token saved_tok = ctx.pending_tok;
    bool ok = lexer_next_token(ctx, out_tok);
    ctx.current_line = saved_line;
    ctx.current_pos = saved_pos;
    ctx.line_no = saved_line_no;
    ctx.has_pending = saved_pending;
    ctx.pending_tok = saved_tok;
    return ok;
}

token_type lexer_instruction_to_type(const std::string& name) {
    auto it = instr_map.find(to_lower(name));
    if (it != instr_map.end()) return it->second;
    auto it2 = multi_instr_map.find(to_lower(name));
    if (it2 != multi_instr_map.end()) return it2->second;
    return TOK_UNKNOWN;
}

token_type lexer_register_to_type(const std::string& name) {
    auto it = reg_map.find(to_lower(name));
    return (it != reg_map.end()) ? it->second : TOK_UNKNOWN;
}

token_type lexer_sysreg_to_type(const std::string& name) {
    auto it = sysreg_map.find(to_lower(name));
    return (it != sysreg_map.end()) ? it->second : TOK_UNKNOWN;
}

} // namespace dasm
