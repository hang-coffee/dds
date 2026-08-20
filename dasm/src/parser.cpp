#include "parser.h"
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <cctype>

namespace dasm {

static std::string to_lower(const std::string& s) {
    std::string res = s;
    for (char& c : res) c = std::tolower(static_cast<unsigned char>(c));
    return res;
}

static bool is_instruction_token(token_type type) {
    return (type >= TOK_INSTR_LET && type <= TOK_INSTR_D2F);
}

static bool is_size_token(token_type type) {
    return (type == TOK_SIZE_BYTE || type == TOK_SIZE_WORD ||
            type == TOK_SIZE_DWORD || type == TOK_SIZE_QWORD);
}

static uint32_t size_token_to_bytes(token_type type) {
    switch (type) {
        case TOK_SIZE_BYTE:  return 1;
        case TOK_SIZE_WORD:  return 2;
        case TOK_SIZE_DWORD: return 4;
        case TOK_SIZE_QWORD: return 8;
        default: return 0;
    }
}

static bool is_register_token(token_type type) {
    return (type >= TOK_REG_A && type <= TOK_REG_DP7);
}

static bool is_sysreg_token(token_type type) {
    return (type >= TOK_SYSREG_CBASE && type <= TOK_SYSREG_FPCR);
}

static bool is_special_no_operand_instruction(token_type type) {
    switch (type) {
        case TOK_INSTR_CSI:
        case TOK_INSTR_CDI:
        case TOK_INSTR_JMP:
        case TOK_INSTR_PUSHR:
        case TOK_INSTR_POPR:
        case TOK_INSTR_SRA:
        case TOK_INSTR_SRB:
        case TOK_INSTR_DIV_QWORD:
        case TOK_INSTR_PUSH_RIN1:
        case TOK_INSTR_PUSH_RIN2:
        case TOK_INSTR_POP_RIN1:
        case TOK_INSTR_POP_RIN2:
        case TOK_INSTR_PUSHI:
        case TOK_INSTR_POPI:
        case TOK_INSTR_SVC:
        case TOK_INSTR_IRET:
        case TOK_INSTR_RER:
        case TOK_INSTR_NOP:
        case TOK_INSTR_HLT:
        case TOK_INSTR_JZ:
        case TOK_INSTR_JNZ:
        case TOK_INSTR_PUSH_P:      // pushp: 操作数隐含为 P
            return true;
        default:
            return false;
    }
}

static bool is_single_operand_instruction(token_type type) {
    switch (type) {
        case TOK_INSTR_ZERO:
        case TOK_INSTR_NEG:
        case TOK_INSTR_MNE:
        case TOK_INSTR_INC:
        case TOK_INSTR_DEC:
		case TOK_INSTR_PUSH:
		case TOK_INSTR_POP:
        case TOK_INSTR_POR:
		case TOK_INSTR_TEST:
		case TOK_INSTR_SFA:
		case TOK_INSTR_CMP:
		case TOK_INSTR_JRZ:
		case TOK_INSTR_JRNZ:
		case TOK_INSTR_JL:
		case TOK_INSTR_JNL:
		case TOK_INSTR_JG:
		case TOK_INSTR_JNG:
		case TOK_INSTR_JB:
		case TOK_INSTR_JNB:
		case TOK_INSTR_JA:
		case TOK_INSTR_JNA:
		case TOK_INSTR_LOD:
		case TOK_INSTR_STO:
		case TOK_INSTR_INT:
		case TOK_INSTR_BLKS:
		case TOK_INSTR_BLKIN:
		case TOK_INSTR_FSQRT:
		case TOK_INSTR_FNEG:
		case TOK_INSTR_FABS:
		case TOK_INSTR_FPUSH:
		case TOK_INSTR_FPOP:
		case TOK_INSTR_DSQRT:
		case TOK_INSTR_DNEG:
		case TOK_INSTR_DABS:
		case TOK_INSTR_DPUSH:
		case TOK_INSTR_DPOP:
        return true;
        default:
            return false;
    }
}

// manual 中标注“需要尺寸”的指令：汇编语法必须带 BYTE/WORD/DWORD
static bool instruction_requires_size(token_type type) {
    switch (type) {
        case TOK_INSTR_LET:
        case TOK_INSTR_LR:
        case TOK_INSTR_ST:
        case TOK_INSTR_ADD:
        case TOK_INSTR_SUB:
        case TOK_INSTR_MUL:
        case TOK_INSTR_DIV:
        case TOK_INSTR_SHL:
        case TOK_INSTR_SHR:
        case TOK_INSTR_MSL:
        case TOK_INSTR_MSR:
        case TOK_INSTR_AND:
        case TOK_INSTR_OR:
        case TOK_INSTR_XOR:
        case TOK_INSTR_MNE:
        case TOK_INSTR_PUSH:
        case TOK_INSTR_POP:
        case TOK_INSTR_SFA:
        case TOK_INSTR_LOD:
        case TOK_INSTR_STO:
        case TOK_INSTR_SR:
        case TOK_INSTR_TEST:
        case TOK_INSTR_CMP:
        case TOK_INSTR_JRZ:
        case TOK_INSTR_JRNZ:
        case TOK_INSTR_JA:
        case TOK_INSTR_JNA:
        case TOK_INSTR_JB:
        case TOK_INSTR_JNB:
        case TOK_INSTR_JG:
        case TOK_INSTR_JNG:
        case TOK_INSTR_JL:
        case TOK_INSTR_JNL:
        case TOK_INSTR_IN:
        case TOK_INSTR_OUT:
        case TOK_INSTR_BLKS:
        case TOK_INSTR_BLKIN:
        case TOK_INSTR_POR:
        case TOK_INSTR_PUSH_P:
            return true;
        default:
            return false;
    }
}

static uint32_t operand_to_register_encoding(token_type type) {
    switch (type) {
        case TOK_REG_A:  return 0x0;
        case TOK_REG_B:  return 0x1;
        case TOK_REG_C:  return 0x2;
        case TOK_REG_D1: return 0x3;
        case TOK_REG_D2: return 0x4;
        case TOK_REG_S:  return 0x5;
        case TOK_REG_T:  return 0x6;
        case TOK_REG_F:  return 0x7;
        case TOK_REG_E:  return 0x8;
        case TOK_REG_R:  return 0x9;
        case TOK_REG_X:  return 0xA;
        case TOK_REG_I:  return 0xB;
        case TOK_REG_FP0: return 0x0;
        case TOK_REG_FP1: return 0x1;
        case TOK_REG_FP2: return 0x2;
        case TOK_REG_FP3: return 0x3;
        case TOK_REG_FP4: return 0x4;
        case TOK_REG_FP5: return 0x5;
        case TOK_REG_FP6: return 0x6;
        case TOK_REG_FP7: return 0x7;
        case TOK_REG_DP0: return 0x0;
        case TOK_REG_DP1: return 0x1;
        case TOK_REG_DP2: return 0x2;
        case TOK_REG_DP3: return 0x3;
        case TOK_REG_DP4: return 0x4;
        case TOK_REG_DP5: return 0x5;
        case TOK_REG_DP6: return 0x6;
        case TOK_REG_DP7: return 0x7;
        default: return 0xF;
    }
}

static uint32_t sysreg_to_encoding(token_type type) {
    switch (type) {
        case TOK_SYSREG_CBASE:      return 0x0;
        case TOK_SYSREG_CLIMIT:     return 0x1;
        case TOK_SYSREG_DBASE:      return 0x2;
        case TOK_SYSREG_DLIMIT:     return 0x3;
        case TOK_SYSREG_KSP:        return 0x4;
        case TOK_SYSREG_RIN3_CTRL:  return 0x5;
        case TOK_SYSREG_XAR:        return 0x6;
        case TOK_SYSREG_ICTB:       return 0x7;
        case TOK_SYSREG_FPCR:       return 0x8;
        default: return 0xF;
    }
}

void parser_init(parser_context& ctx, const std::vector<token>& tokens,
                 symbol_table& symtab, generator_context& gen) {
    ctx.tokens = &tokens;
    ctx.current_token = 0;
    ctx.symtab = &symtab;
    ctx.gen = &gen;
    ctx.current_line = 1;
    ctx.errors.clear();
    ctx.is_pass1 = true;
}

const token* parser_peek(parser_context& ctx) {
    if (ctx.current_token >= ctx.tokens->size()) return nullptr;
    return &(*ctx.tokens)[ctx.current_token];
}

bool parser_consume(parser_context& ctx) {
    if (ctx.current_token >= ctx.tokens->size()) return false;
    ctx.current_line = (*ctx.tokens)[ctx.current_token].line_no;
    ctx.current_token++;
    return true;
}

bool parser_is_eof(parser_context& ctx) {
    if (ctx.current_token >= ctx.tokens->size()) return true;
    const token* tok = parser_peek(ctx);
    return (tok && tok->type == TOK_EOF);
}

static void parser_error(parser_context& ctx, const std::string& msg) {
    ctx.errors.push_back("行 " + std::to_string(ctx.current_line) + ": " + msg);
}

static bool expect_token(parser_context& ctx, token_type expected, const std::string& msg) {
    const token* tok = parser_peek(ctx);
    if (!tok || tok->type != expected) {
        parser_error(ctx, msg);
        return false;
    }
    parser_consume(ctx);
    return true;
}

// ============================================================
// 操作数解析
// ============================================================

// 立即数表达式：IMMEDIATE | LABEL_REF | DOLLAR [(+|-) 项 ...]
// 支持如 0x100 - $、label+4、$+0x10 等
struct imm_expr {
    bool valid;
    std::vector<const token*> terms;   // 项（IMMEDIATE / LABEL_REF / DOLLAR）
    std::vector<char> ops;             // ops[i] 是 terms[i+1] 前的运算符
    bool has_extern;                   // 表达式中是否引用了 EXTERN 符号
    std::string extern_name;           // 引用的外部符号名（仅单个外部符号）
};

// 一个已解析的操作数
struct parsed_operand {
    const token* tok;        // 主 token（寄存器 / 立即数类首项 / sysreg）
    bool is_deref;           // *reg
    bool has_offset;         // *reg+N
    imm_expr offset_expr;    // 偏移表达式（has_offset 时有效）
    int64_t offset_value;    // 偏移求值结果
    bool is_imm;             // 立即数类（含表达式）
    imm_expr imm_ex;         // 立即数表达式
    int64_t imm_value;       // 立即数表达式求值结果
    bool is_string;          // 字符串立即数
    bool has_extern;         // 立即数表达式中是否引用了 EXTERN 符号
    std::string extern_name; // 引用的外部符号名
};

static bool imm_expr_term(parser_context& ctx, const token*& tok) {
    tok = parser_peek(ctx);
    if (!tok) return false;
    if (tok->type == TOK_IMMEDIATE || tok->type == TOK_LABEL_REF || tok->type == TOK_DOLLAR) {
        parser_consume(ctx);
        return true;
    }
    return false;
}

static bool parse_imm_expr(parser_context& ctx, imm_expr& out) {
    out.terms.clear();
    out.ops.clear();
    out.valid = false;
    out.has_extern = false;
    out.extern_name.clear();
    const token* t = nullptr;
    if (!imm_expr_term(ctx, t)) return false;
    out.terms.push_back(t);
    while (true) {
        const token* n = parser_peek(ctx);
        if (!n) break;
        if (n->type == TOK_PLUS || n->type == TOK_MINUS) {
            parser_consume(ctx);
            const token* t2 = nullptr;
            if (!imm_expr_term(ctx, t2)) {
                parser_error(ctx, "表达式缺少操作数（+/- 后需要立即数或标号）");
                return false;
            }
            out.ops.push_back(n->type == TOK_PLUS ? '+' : '-');
            out.terms.push_back(t2);
        } else {
            break;
        }
    }
    out.valid = true;
    return true;
}

static bool eval_imm_expr(parser_context& ctx, imm_expr& expr, int64_t& value) {
    if (!expr.valid) return false;
    value = 0;
    expr.has_extern = false;
    expr.extern_name.clear();
    for (size_t i = 0; i < expr.terms.size(); i++) {
        const token* t = expr.terms[i];
        int64_t term = 0;
        if (t->type == TOK_IMMEDIATE) {
            term = static_cast<int64_t>(t->value);
        } else if (t->type == TOK_DOLLAR) {
            term = static_cast<int64_t>(symbol_get_current_address(*ctx.symtab));
        } else if (t->type == TOK_LABEL_REF) {
            const symbol* sym = symbol_lookup(*ctx.symtab, t->text);
            if (!sym) {
                parser_error(ctx, "未定义标号: " + t->text);
                return false;
            }
            if (sym->external) {
                // EXTERN 符号：地址在链接期决定，这里先按 0 占位
                expr.has_extern = true;
                expr.extern_name = t->text;
                term = 0;
            } else {
                term = static_cast<int64_t>(sym->address);
            }
        }
        if (i == 0) value = term;
        else value += (expr.ops[i - 1] == '+') ? term : -term;
    }
    return true;
}

// 解析并（按需）求值一个立即数表达式。
// need_value=false 时（pass1 中值不影响长度的场景）只消费 token 不求值，
// 以便前向标号在 pass1 不报错。
static bool parse_imm_expr_value(parser_context& ctx, imm_expr& expr,
                                 int64_t& value, bool need_value) {
    if (!parse_imm_expr(ctx, expr)) return false;
    if (need_value || !ctx.is_pass1) {
        if (!eval_imm_expr(ctx, expr, value)) return false;
    }
    return true;
}

static bool parse_operand(parser_context& ctx, parsed_operand& out) {
    out.tok = nullptr;
    out.is_deref = false;
    out.has_offset = false;
    out.offset_expr.valid = false;
    out.offset_value = 0;
    out.is_imm = false;
    out.imm_ex.valid = false;
    out.imm_value = 0;
    out.is_string = false;
    out.has_extern = false;
    out.extern_name.clear();

    const token* tok = parser_peek(ctx);
    if (!tok) return false;

    if (tok->type == TOK_STAR) {
        parser_consume(ctx);
        const token* reg_tok = parser_peek(ctx);
        if (!reg_tok || !is_register_token(reg_tok->type)) {
            parser_error(ctx, "* 后需要寄存器");
            return false;
        }
        out.tok = reg_tok;
        out.is_deref = true;
        parser_consume(ctx);
        // *reg+N / *reg-N 指针偏移
        const token* n = parser_peek(ctx);
        if (n && (n->type == TOK_PLUS || n->type == TOK_MINUS)) {
            bool neg = (n->type == TOK_MINUS);
            parser_consume(ctx);
            int64_t offv = 0;
            if (!parse_imm_expr_value(ctx, out.offset_expr, offv, !ctx.is_pass1)) return false;
            out.has_offset = true;
            out.offset_value = neg ? -offv : offv;
        }
        return true;
    }

    if (is_register_token(tok->type) || is_sysreg_token(tok->type)) {
        out.tok = tok;
        parser_consume(ctx);
        return true;
    }

    if (tok->type == TOK_STRING) {
        out.tok = tok;
        out.is_string = true;
        parser_consume(ctx);
        return true;
    }

    if (tok->type == TOK_IMMEDIATE || tok->type == TOK_LABEL_REF || tok->type == TOK_DOLLAR) {
        int64_t v = 0;
        if (!parse_imm_expr_value(ctx, out.imm_ex, v, !ctx.is_pass1)) return false;
        out.tok = out.imm_ex.terms.empty() ? nullptr : out.imm_ex.terms[0];
        out.is_imm = true;
        out.imm_value = v;
        out.has_extern = out.imm_ex.has_extern;
        out.extern_name = out.imm_ex.extern_name;
        return true;
    }

    return false;
}

static bool parse_sr_expression(parser_context& ctx,
                                const token*& base,
                                const token*& index,
                                uint32_t& scale_pow,
                                imm_expr& offset,
                                int64_t& offset_value) {
    base = index = nullptr;
    scale_pow = 0;
    offset.valid = false;
    offset_value = 0;
    const token* tok = parser_peek(ctx);
    if (!tok) return false;
    if (is_register_token(tok->type) || tok->type == TOK_LABEL_REF) {
        base = tok;
        parser_consume(ctx);
    } else {
        parser_error(ctx, "SR 需要基址操作数");
        return false;
    }
    tok = parser_peek(ctx);
    if (tok && tok->type == TOK_PLUS) {
        parser_consume(ctx);
        tok = parser_peek(ctx);
        if (tok && is_register_token(tok->type)) {
            index = tok;
            parser_consume(ctx);
            tok = parser_peek(ctx);
            if (tok && tok->type == TOK_STAR) {
                parser_consume(ctx);
                tok = parser_peek(ctx);
                if (tok && tok->type == TOK_IMMEDIATE) {
                    uint32_t k = static_cast<uint32_t>(tok->value);
                    scale_pow = 0;
                    while ((k >> scale_pow) > 1) scale_pow++;
                    parser_consume(ctx);
                } else {
                    parser_error(ctx, "SR 的 * 后需要比例系数");
                    return false;
                }
            }
            tok = parser_peek(ctx);
            if (tok && (tok->type == TOK_PLUS || tok->type == TOK_MINUS)) {
                parser_consume(ctx);
                if (!parse_imm_expr_value(ctx, offset, offset_value, !ctx.is_pass1)) return false;
            }
        } else {
            if (tok && (tok->type == TOK_IMMEDIATE || tok->type == TOK_LABEL_REF || tok->type == TOK_DOLLAR)) {
                if (!parse_imm_expr_value(ctx, offset, offset_value, !ctx.is_pass1)) return false;
            } else {
                parser_error(ctx, "SR 的 + 后需要偏移量");
                return false;
            }
        }
    }
    return true;
}

// 操作数合法性校验（两遍扫描共用）
static bool validate_operands(parser_context& ctx, token_type instr_type,
                              const parsed_operand& op1, const parsed_operand& op2) {
    // P 寄存器只允许出现在 PUSH P（此前的 PUSH_P 转换）中
    auto is_p = [](const parsed_operand& op) {
        return op.tok && op.tok->type == TOK_REG_P;
    };
    if (is_p(op1) || is_p(op2)) {
        parser_error(ctx, "寄存器 P 只可用于 PUSH P");
        return false;
    }
    // 解引用 *reg 仅 LR(op2)/ST(op1)/POR(op1) 允许；偏移 +N 仅 LR(op2)/ST(op1)
    bool deref_ok1 = (instr_type == TOK_INSTR_ST || instr_type == TOK_INSTR_POR ||
                      instr_type == TOK_INSTR_FST || instr_type == TOK_INSTR_DST);
    bool deref_ok2 = (instr_type == TOK_INSTR_LR || instr_type == TOK_INSTR_FLD ||
                      instr_type == TOK_INSTR_DLD);
    bool off_ok1   = (instr_type == TOK_INSTR_ST);
    bool off_ok2   = (instr_type == TOK_INSTR_LR);
    if (op1.is_deref && !deref_ok1) { parser_error(ctx, "该指令不支持 * 解引用操作数"); return false; }
    if (op2.is_deref && !deref_ok2) { parser_error(ctx, "该指令不支持 * 解引用操作数"); return false; }
    if (op1.has_offset && !off_ok1) { parser_error(ctx, "该指令不支持指针偏移 *reg+N"); return false; }
    if (op2.has_offset && !off_ok2) { parser_error(ctx, "该指令不支持指针偏移 *reg+N"); return false; }
    return true;
}

static uint32_t calculate_instruction_length(token_type instr_type,
                                             token_type size_type,
                                             const parsed_operand* op1,
                                             const parsed_operand* op2) {
    uint32_t total = 2; // Byte0 + Byte1
    // PUSH_P（pushp 或 PUSH P）也需要 Byte2 操作数表（0xEE），与 encoder/decode 一致
    bool has_operand_table = !is_special_no_operand_instruction(instr_type)
                             || instr_type == TOK_INSTR_PUSH_P;
    if (has_operand_table) total += 1; // Byte2

    bool has_imm = false;
    if (op1 && (op1->is_imm || op1->is_string)) has_imm = true;
    if (op2 && (op2->is_imm || op2->is_string)) has_imm = true;
    if (op1 && op1->has_offset) has_imm = true;
    if (op2 && op2->has_offset) has_imm = true;

    if (instr_type == TOK_INSTR_SR) {
        uint32_t imm_bytes = (size_type != TOK_UNKNOWN) ? size_token_to_bytes(size_type) : 4;
        total = 2 + (has_operand_table ? 1 : 0) + 1 + imm_bytes;
    } else if (has_imm) {
        uint32_t imm_bytes = (size_type != TOK_UNKNOWN) ? size_token_to_bytes(size_type) : 4;
        if (instr_type == TOK_INSTR_DLDI) imm_bytes = 8;
        // 字符串立即数按实际字节数
        if (op1 && op1->is_string && op1->tok) imm_bytes = static_cast<uint32_t>(op1->tok->bytes.size());
        else if (op2 && op2->is_string && op2->tok) imm_bytes = static_cast<uint32_t>(op2->tok->bytes.size());
        total += imm_bytes;
    }
    return total;
}

// 在解析完指令与操作数后统一处理：PUSH P → PUSH_P、pushp 默认尺寸、INT 默认 BYTE
static token_type finalize_instr_type(parser_context& ctx, token_type instr_type,
                                      parsed_operand& op1,
                                      token_type& size_type) {
    // PUSH P（操作数为 P 寄存器）→ PUSH_P（操作数隐含）
    if (instr_type == TOK_INSTR_PUSH && op1.tok && op1.tok->type == TOK_REG_P) {
        instr_type = TOK_INSTR_PUSH_P;
        op1 = parsed_operand();
        if (size_type == TOK_UNKNOWN) size_type = TOK_SIZE_DWORD;
    }
    // pushp 关键字（无尺寸）默认 DWORD
    if (instr_type == TOK_INSTR_PUSH_P && size_type == TOK_UNKNOWN) size_type = TOK_SIZE_DWORD;
    // INT N：manual 规定 N 是一个 BYTE，未给尺寸时按 BYTE 编码
    if (instr_type == TOK_INSTR_INT && op1.is_imm && size_type == TOK_UNKNOWN) size_type = TOK_SIZE_BYTE;
    // DFE：浮点立即数/内存访问固定为 32 位
    if (instr_type == TOK_INSTR_FLDI && size_type == TOK_UNKNOWN) size_type = TOK_SIZE_DWORD;
    if (instr_type == TOK_INSTR_DLDI && size_type == TOK_UNKNOWN) size_type = TOK_SIZE_DWORD;
    if (instr_type == TOK_INSTR_FLD && size_type == TOK_UNKNOWN) size_type = TOK_SIZE_DWORD;
    if (instr_type == TOK_INSTR_FST && size_type == TOK_UNKNOWN) size_type = TOK_SIZE_DWORD;
    // manual 标注“需要尺寸”的指令缺少尺寸 → 报错
    if (instruction_requires_size(instr_type) && size_type == TOK_UNKNOWN) {
        parser_error(ctx, "该指令需要显式尺寸 BYTE/WORD/DWORD");
        return TOK_UNKNOWN;
    }
    return instr_type;
}

static bool parse_instruction_pass1(parser_context& ctx) {
    const token* tok = parser_peek(ctx);
    if (!tok) return false;

    // 检测 REP 前缀 (REP XCHG 形式; manual规定REP是前缀标志)
    // pass1 只关心长度（与 REP 无关），只需消费 token
    if (tok && tok->type == TOK_REP_PREFIX) {
        parser_consume(ctx);
        tok = parser_peek(ctx);
    }
    if (!tok) return false;

    token_type instr_type = tok->type;
    ctx.current_line = tok->line_no;
    parser_consume(ctx);

    // 检测 REP 后缀 (兼容 XCHG REP 形式)
    tok = parser_peek(ctx);
    if (tok && tok->type == TOK_REP_PREFIX) {
        parser_consume(ctx);
    }

    // NZ 后缀（pass1 不参与长度计算，只需消费）
    tok = parser_peek(ctx);
    if (tok && tok->type == TOK_NZ_SUFFIX) {
        parser_consume(ctx);
    }

    token_type size_type = TOK_UNKNOWN;
    tok = parser_peek(ctx);
    if (tok && is_size_token(tok->type)) {
        size_type = tok->type;
        parser_consume(ctx);
    }

    parsed_operand op1 = {};
    parsed_operand op2 = {};
    uint32_t scale_pow = 0;
    const token* sr_base = nullptr;
    const token* sr_index = nullptr;
    imm_expr sr_offset = {};
    int64_t sr_offset_value = 0;

    if (is_special_no_operand_instruction(instr_type)) {
        // no operands
    } else if (instr_type == TOK_INSTR_SR) {
        if (!parse_sr_expression(ctx, sr_base, sr_index, scale_pow, sr_offset, sr_offset_value))
            return false;
    } else if (is_single_operand_instruction(instr_type)) {
        if (!parse_operand(ctx, op1)) {
            parser_error(ctx, "指令缺少操作数");
            return false;
        }
    } else {
        // 双操作数
        if (!parse_operand(ctx, op1)) {
            parser_error(ctx, "指令缺少第一个操作数");
            return false;
        }
        tok = parser_peek(ctx);
        if (tok && tok->type == TOK_COMMA) {
            parser_consume(ctx);
            tok = parser_peek(ctx);
            if (tok && is_size_token(tok->type)) {
                size_type = tok->type;
                parser_consume(ctx);
            }
            if (!parse_operand(ctx, op2)) {
                parser_error(ctx, "指令缺少第二个操作数");
                return false;
            }
        } else {
            parser_error(ctx, "两个操作数之间需要逗号");
            return false;
        }
    }

    instr_type = finalize_instr_type(ctx, instr_type, op1, size_type);
    if (instr_type == TOK_UNKNOWN) return false;
    if (!validate_operands(ctx, instr_type, op1, op2)) return false;

    uint32_t length = calculate_instruction_length(instr_type, size_type, &op1, &op2);
    symbol_advance(*ctx.symtab, length);
    return true;
}

static bool parse_pseudo_pass1(parser_context& ctx) {
    const token* tok = parser_peek(ctx);
    if (!tok) return false;
    ctx.current_line = tok->line_no;

    if (tok->type == TOK_PSEUDO_SECTION) {
        parser_consume(ctx);
        tok = parser_peek(ctx);
        if (!tok || tok->type != TOK_LABEL_REF) {
            parser_error(ctx, "SECTION 后需要段名");
            return false;
        }
        std::string seg = to_lower(tok->text);
        if (seg == "text") {
            symbol_set_segment(*ctx.symtab, SEG_TEXT);
        } else if (seg == "data") {
            symbol_set_segment(*ctx.symtab, SEG_DATA);
        } else {
            parser_error(ctx, "无效段名: " + tok->text);
            return false;
        }
        parser_consume(ctx);
        return true;
    }

    if (tok->type == TOK_PSEUDO_ORG) {
        parser_consume(ctx);
        imm_expr e;
        int64_t v = 0;
        if (!parse_imm_expr_value(ctx, e, v, true)) return false;
        uint32_t base = static_cast<uint32_t>(v);
        uint32_t cur = symbol_get_current_address(*ctx.symtab);
        if (base < cur) {
            parser_error(ctx, "ORG 地址回退（当前偏移已超过 " + std::to_string(base) + "）");
            return false;
        }
        symbol_set_org(*ctx.symtab, ctx.symtab->current_segment, base);
        return true;
    }

    if (tok->type >= TOK_PSEUDO_DB && tok->type <= TOK_PSEUDO_DQ) {
        token_type data_type = tok->type;
        parser_consume(ctx);

        // 地址（支持表达式；定位数据到该地址，之前填充 0x00）
        imm_expr addr_expr;
        int64_t addr_value = 0;
        if (!parse_imm_expr_value(ctx, addr_expr, addr_value, true)) return false;

        if (!expect_token(ctx, TOK_COMMA, "地址后需要逗号")) return false;

        tok = parser_peek(ctx);
        if (!tok) {
            parser_error(ctx, "DB/DW/DD/DQ 后缺少数据");
            return false;
        }

        uint32_t data_len = 0;
        if (tok->type == TOK_STRING) {
            data_len = static_cast<uint32_t>(tok->bytes.size());
        } else if (tok->type == TOK_IMMEDIATE || tok->type == TOK_LABEL_REF || tok->type == TOK_DOLLAR) {
            switch (data_type) {
                case TOK_PSEUDO_DB: data_len = 1; break;
                case TOK_PSEUDO_DW: data_len = 2; break;
                case TOK_PSEUDO_DD: data_len = 4; break;
                case TOK_PSEUDO_DQ: data_len = 8; break;
                default: break;
            }
        } else {
            parser_error(ctx, "无效的数据类型");
            return false;
        }

        uint32_t cur = symbol_get_current_address(*ctx.symtab);
        uint32_t addr = static_cast<uint32_t>(addr_value);
        if (addr < cur) {
            parser_error(ctx, "数据地址回退（" + std::to_string(addr) + " < 当前偏移 " + std::to_string(cur) + "）");
            return false;
        }
        symbol_advance(*ctx.symtab, (addr - cur) + data_len);
        // 数据值（pass1 只消费不求值，长度由类型决定）
        if (tok->type == TOK_STRING) {
            parser_consume(ctx);
        } else {
            imm_expr dv;
            int64_t dv_value = 0;
            if (!parse_imm_expr_value(ctx, dv, dv_value, false)) return false;
        }
        return true;
    }

    if (tok->type == TOK_PSEUDO_RESB) {
        parser_consume(ctx);
        imm_expr e;
        int64_t v = 0;
        if (!parse_imm_expr_value(ctx, e, v, true)) return false;
        if (v < 0) {
            parser_error(ctx, "RESB 数量为负（表达式结果为 " + std::to_string(v) + "）");
            return false;
        }
        symbol_advance(*ctx.symtab, static_cast<uint32_t>(v));
        return true;
    }

    if (tok->type == TOK_PSEUDO_EXTERN) {
        parser_consume(ctx);
        tok = parser_peek(ctx);
        if (!tok || tok->type != TOK_LABEL_REF) {
            parser_error(ctx, "EXTERN 后需要标号");
            return false;
        }
        if (!symbol_add_extern(*ctx.symtab, tok->text)) {
            parser_error(ctx, "EXTERN 声明失败: " + tok->text);
            return false;
        }
        parser_consume(ctx);
        return true;
    }

    parser_error(ctx, "未知伪指令");
    return false;
}

bool parser_pass1(parser_context& ctx) {
    ctx.is_pass1 = true;
    symbol_init(*ctx.symtab);
    symbol_set_segment(*ctx.symtab, SEG_TEXT);

    while (!parser_is_eof(ctx)) {
        const token* tok = parser_peek(ctx);
        if (!tok) break;

        ctx.current_line = tok->line_no;

        if (tok->type == TOK_LABEL_DEF) {
            if (!symbol_add_label(*ctx.symtab, tok->text)) {
                parser_error(ctx, "标号重复定义: " + tok->text);
            }
            parser_consume(ctx);
            continue;
        }

        if (tok->type >= TOK_PSEUDO_SECTION && tok->type <= TOK_PSEUDO_EXTERN) {
            if (!parse_pseudo_pass1(ctx)) return false;
            continue;
        }

        if (tok->type == TOK_REP_PREFIX || is_instruction_token(tok->type)) {
            if (!parse_instruction_pass1(ctx)) return false;
            continue;
        }

        parser_error(ctx, "意外的 Token: " + tok->text);
        parser_consume(ctx);
    }

    return ctx.errors.empty();
}

bool parser_pass2(parser_context& ctx) {
    ctx.is_pass1 = false;
    ctx.current_token = 0;
    generator_init(*ctx.gen);
    symbol_reset_offsets(*ctx.symtab);   // 保留符号表，但偏移从 0 重新累计
    symbol_set_segment(*ctx.symtab, SEG_TEXT);

    while (!parser_is_eof(ctx)) {
        const token* tok = parser_peek(ctx);
        if (!tok) break;

        ctx.current_line = tok->line_no;

        if (tok->type == TOK_LABEL_DEF) {
            parser_consume(ctx);
            continue;
        }

        // 伪指令
        if (tok->type >= TOK_PSEUDO_SECTION && tok->type <= TOK_PSEUDO_EXTERN) {
            if (tok->type == TOK_PSEUDO_SECTION) {
                parser_consume(ctx);
                tok = parser_peek(ctx);
                if (tok && tok->type == TOK_LABEL_REF) {
                    std::string seg = to_lower(tok->text);
                    if (seg == "text") {
                        symbol_set_segment(*ctx.symtab, SEG_TEXT);
                        generator_set_segment(*ctx.gen, SEG_TEXT);
                    } else if (seg == "data") {
                        symbol_set_segment(*ctx.symtab, SEG_DATA);
                        generator_set_segment(*ctx.gen, SEG_DATA);
                    }
                    parser_consume(ctx);
                }
                continue;
            }

            if (tok->type == TOK_PSEUDO_ORG) {
                parser_consume(ctx);
                imm_expr e;
                int64_t v = 0;
                if (!parse_imm_expr_value(ctx, e, v, true)) return false;
                uint32_t base = static_cast<uint32_t>(v);
                uint32_t cur = symbol_get_current_address(*ctx.symtab);
                if (base < cur) {
                    parser_error(ctx, "ORG 地址回退（当前偏移已超过 " + std::to_string(base) + "）");
                    return false;
                }
                symbol_set_org(*ctx.symtab, ctx.symtab->current_segment, base);
                generator_pad_to(*ctx.gen, base);   // ORG 填充输出缓冲（B1.5）
                continue;
            }

            if (tok->type >= TOK_PSEUDO_DB && tok->type <= TOK_PSEUDO_DQ) {
                token_type data_type = tok->type;
                parser_consume(ctx);

                imm_expr addr_expr;
                int64_t addr_value = 0;
                if (!parse_imm_expr_value(ctx, addr_expr, addr_value, true)) return false;

                if (!expect_token(ctx, TOK_COMMA, "地址后需要逗号")) return false;

                tok = parser_peek(ctx);
                if (!tok) {
                    parser_error(ctx, "DB/DW/DD/DQ 后缺少数据");
                    return false;
                }

                uint32_t bytes = 0;
                switch (data_type) {
                    case TOK_PSEUDO_DB: bytes = 1; break;
                    case TOK_PSEUDO_DW: bytes = 2; break;
                    case TOK_PSEUDO_DD: bytes = 4; break;
                    case TOK_PSEUDO_DQ: bytes = 8; break;
                    default: break;
                }

                uint32_t cur = symbol_get_current_address(*ctx.symtab);
                uint32_t addr = static_cast<uint32_t>(addr_value);
                if (addr < cur) {
                    parser_error(ctx, "数据地址回退（" + std::to_string(addr) + " < 当前偏移 " + std::to_string(cur) + "）");
                    return false;
                }

                if (tok->type == TOK_STRING) {
                    generator_pad_to(*ctx.gen, addr);
                    generator_emit_data(*ctx.gen, tok->bytes.data(), tok->bytes.size());
                    symbol_advance(*ctx.symtab, (addr - cur) + tok->bytes.size());
                    parser_consume(ctx);   // 消费字符串 token
                } else if (tok->type == TOK_IMMEDIATE || tok->type == TOK_LABEL_REF || tok->type == TOK_DOLLAR) {
                    imm_expr dv;
                    int64_t dv_value = 0;
                    if (!parse_imm_expr_value(ctx, dv, dv_value, true)) return false;
                    if (dv.has_extern) {
                        if (bytes != 1 && bytes != 2 && bytes != 4) {
                            parser_error(ctx, "EXTERN 符号仅支持 1/2/4 字节数据字段");
                            return false;
                        }
                        generator_add_relocation(*ctx.gen, dv.extern_name, addr,
                                                 ctx.gen->current_segment, bytes);
                    }
                    generator_pad_to(*ctx.gen, addr);
                    generator_emit_immediate(*ctx.gen, static_cast<uint32_t>(dv_value), bytes);
                    symbol_advance(*ctx.symtab, (addr - cur) + bytes);
                } else {
                    parser_error(ctx, "无效的数据类型");
                    return false;
                }
                continue;
            }

            if (tok->type == TOK_PSEUDO_RESB) {
                parser_consume(ctx);
                imm_expr e;
                int64_t v = 0;
                if (!parse_imm_expr_value(ctx, e, v, true)) return false;
                if (v < 0) {
                    parser_error(ctx, "RESB 数量为负（表达式结果为 " + std::to_string(v) + "）");
                    return false;
                }
                uint32_t num = static_cast<uint32_t>(v);
                generator_emit_reserve(*ctx.gen, num);
                symbol_advance(*ctx.symtab, num);
                continue;
            }

            if (tok->type == TOK_PSEUDO_EXTERN) {
                parser_consume(ctx);
                tok = parser_peek(ctx);
                if (!tok || tok->type != TOK_LABEL_REF) {
                    parser_error(ctx, "EXTERN 后需要标号");
                    return false;
                }
                parser_consume(ctx);
                continue;
            }

            parser_error(ctx, "未知伪指令");
            return false;
        }

        // 指令 (支持 REP 前缀: REP XCHG; 也兼容后缀 XCHG REP)
        if (tok->type == TOK_REP_PREFIX || is_instruction_token(tok->type)) {
            bool has_rep = false;
            if (tok->type == TOK_REP_PREFIX) {
                has_rep = true;
                parser_consume(ctx);
                tok = parser_peek(ctx);
            }
            if (!tok) return false;
            token_type instr_type = tok->type;
            parser_consume(ctx);

            // 检测 REP 后缀 (兼容)
            tok = parser_peek(ctx);
            if (tok && tok->type == TOK_REP_PREFIX) {
                has_rep = true;
                parser_consume(ctx);
            }

            bool has_nz = false;
            tok = parser_peek(ctx);
            if (tok && tok->type == TOK_NZ_SUFFIX) {
                has_nz = true;
                parser_consume(ctx);
            }

            token_type size_type = TOK_UNKNOWN;
            tok = parser_peek(ctx);
            if (tok && is_size_token(tok->type)) {
                size_type = tok->type;
                parser_consume(ctx);
            }

            parsed_operand op1 = {};
            parsed_operand op2 = {};
            uint32_t scale_pow = 0;
            const token* sr_base = nullptr;
            const token* sr_index = nullptr;
            imm_expr sr_offset = {};
            int64_t sr_offset_value = 0;

            if (is_special_no_operand_instruction(instr_type)) {
                // no operands
            } else if (instr_type == TOK_INSTR_SR) {
                if (!parse_sr_expression(ctx, sr_base, sr_index, scale_pow, sr_offset, sr_offset_value))
                    return false;
            } else if (is_single_operand_instruction(instr_type)) {
                if (!parse_operand(ctx, op1)) {
                    parser_error(ctx, "指令缺少操作数");
                    return false;
                }
            } else {
                // 双操作数
                if (!parse_operand(ctx, op1)) {
                    parser_error(ctx, "指令缺少第一个操作数");
                    return false;
                }
                if (!expect_token(ctx, TOK_COMMA, "需要逗号")) return false;
                tok = parser_peek(ctx);
                if (tok && is_size_token(tok->type)) {
                    size_type = tok->type;
                    parser_consume(ctx);
                }
                if (!parse_operand(ctx, op2)) {
                    parser_error(ctx, "指令缺少第二个操作数");
                    return false;
                }
            }

            instr_type = finalize_instr_type(ctx, instr_type, op1, size_type);
            if (instr_type == TOK_UNKNOWN) return false;
            // 注意：opcode 必须在 finalize 之后计算（PUSH P 会被转换为 PUSH_P）
            int opcode = static_cast<int>(instr_type) - static_cast<int>(TOK_INSTR_LET);
            if (!validate_operands(ctx, instr_type, op1, op2)) return false;

            uint32_t size_bytes = (size_type != TOK_UNKNOWN) ? size_token_to_bytes(size_type) : 0;

            enc_operand_t eop1 = {};
            enc_operand_t eop2 = {};

            auto fill_operand = [&](const parsed_operand& op, enc_operand_t& eop) {
                if (!op.tok) return;
                if (op.is_imm) {
                    eop.is_immediate = true;
                    eop.value = static_cast<uint64_t>(op.imm_value);
                } else if (op.is_string) {
                    eop.is_immediate = true;
                    eop.is_string = true;
                    eop.string_bytes = op.tok->bytes;
                } else if (is_register_token(op.tok->type)) {
                    eop.value = operand_to_register_encoding(op.tok->type);
                    eop.is_register = true;
                } else if (is_sysreg_token(op.tok->type)) {
                    eop.value = sysreg_to_encoding(op.tok->type);
                    eop.is_sysreg = true;
                }
                // 解引用的编码与寄存器相同（执行时解引用）
                if (op.has_offset) {
                    eop.has_offset = true;
                    eop.offset_value = static_cast<uint32_t>(op.offset_value);
                }
            };
            fill_operand(op1, eop1);
            fill_operand(op2, eop2);

            enc_sr_params_t sr_params = {};
            sr_params.has_sr = (instr_type == TOK_INSTR_SR);
            if (sr_params.has_sr) {
                sr_params.base_is_register = (sr_base && is_register_token(sr_base->type));
                sr_params.base_value = sr_base ? operand_to_register_encoding(sr_base->type) : 0;
                sr_params.index_is_register = (sr_index && is_register_token(sr_index->type));
                sr_params.index_value = sr_index ? operand_to_register_encoding(sr_index->type) : 0;
                sr_params.scale_pow = scale_pow;
                sr_params.offset_is_immediate = sr_offset.valid;
                sr_params.offset_value = static_cast<uint32_t>(sr_offset_value);
            }

            enc_result_t result = encoder_encode(instr_type, opcode,
                                                  has_nz, has_rep,
                                                  size_type, size_bytes,
                                                  eop1, eop2, sr_params);

            if (!result.success) {
                parser_error(ctx, result.error_msg);
                return false;
            }

            // EXTERN 引用：在立即数字段上生成 ELF 重定位
            if (op1.has_extern || op2.has_extern) {
                if (op1.has_extern && op2.has_extern) {
                    parser_error(ctx, "一条指令中不能同时引用两个 EXTERN 符号");
                    return false;
                }
                if (size_bytes != 1 && size_bytes != 2 && size_bytes != 4) {
                    parser_error(ctx, "EXTERN 符号仅支持 1/2/4 字节立即数");
                    return false;
                }
                const std::string& ext_name = op1.has_extern ? op1.extern_name : op2.extern_name;
                uint32_t imm_start = static_cast<uint32_t>(result.bytes.size() - size_bytes);
                uint32_t reloc_off = symbol_get_current_address(*ctx.symtab) + imm_start;
                generator_add_relocation(*ctx.gen, ext_name, reloc_off,
                                         ctx.gen->current_segment, size_bytes);
            }

            generator_emit_bytes(*ctx.gen, result.bytes.data(), result.bytes.size());
            symbol_advance(*ctx.symtab, result.bytes.size());   // pass2 同步偏移（保证 $ 语义）
            continue;
        }

        parser_error(ctx, "意外的 Token: " + tok->text);
        parser_consume(ctx);
    }

    return ctx.errors.empty();
}

bool parser_assemble(parser_context& ctx, bool verbose) {
    if (!parser_pass1(ctx)) {
        if (verbose) {
            std::cerr << "第一遍扫描失败：" << std::endl;
            for (const auto& err : ctx.errors) {
                std::cerr << "  " << err << std::endl;
            }
        }
        return false;
    }

    if (verbose) {
        std::cout << "第一遍扫描成功" << std::endl;
        std::cout << "TEXT 段大小: " << ctx.symtab->text_offset << " 字节" << std::endl;
        std::cout << "DATA 段大小: " << ctx.symtab->data_offset << " 字节" << std::endl;
        symbol_print(*ctx.symtab);
    }

    ctx.errors.clear();

    if (!parser_pass2(ctx)) {
        if (verbose) {
            std::cerr << "第二遍扫描失败：" << std::endl;
            for (const auto& err : ctx.errors) {
                std::cerr << "  " << err << std::endl;
            }
        }
        return false;
    }

    if (verbose) {
        std::cout << "第二遍扫描成功" << std::endl;
        std::cout << "生成 TEXT: " << ctx.gen->code_buffer.size() << " 字节" << std::endl;
        std::cout << "生成 DATA: " << ctx.gen->data_buffer.size() << " 字节" << std::endl;
    }

    return true;
}

} // namespace dasm
