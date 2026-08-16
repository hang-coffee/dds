#include "encoder.h"
#include <cstring>

namespace dasm {

static uint32_t get_size_code(token_type size_type) {
    switch (size_type) {
        case TOK_SIZE_BYTE:  return 1;
        case TOK_SIZE_WORD:  return 2;
        case TOK_SIZE_DWORD: return 3;
        default: return 0;
    }
}

static uint32_t get_immediate_bytes(token_type size_type) {
    switch (size_type) {
        case TOK_SIZE_BYTE:  return 1;
        case TOK_SIZE_WORD:  return 2;
        case TOK_SIZE_DWORD: return 4;
        default: return 4;
    }
}

uint32_t encoder_get_operand_encoding(token_type reg_type) {
    switch (reg_type) {
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
        default: return 0xF;
    }
}

enc_result_t encoder_encode(token_type instr_type,
                            int opcode,
                            bool has_nz,
                            bool has_rep,
                            token_type size_type,
                            uint32_t size_bytes,
                            const enc_operand_t& op1,
                            const enc_operand_t& op2,
                            const enc_sr_params_t& sr_params) {
    enc_result_t result;
    result.success = true;
    result.bytes.clear();
    result.error_msg = "";
    (void)size_bytes;   // 立即数宽度由 size_type 决定

    uint8_t byte0 = 0;

    if (has_rep) byte0 |= 0x80;

    uint32_t size_code = get_size_code(size_type);
    byte0 |= (size_code << 5);

    if (has_nz) byte0 |= 0x10;

    bool has_operand_table = true;
    switch (instr_type) {
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
            has_operand_table = false;
            break;
        default:
            has_operand_table = true;
            break;
    }

    uint32_t extra_bytes = 1; // Byte1
    if (has_operand_table) extra_bytes += 1; // Byte2

    bool has_imm = false;
    if ((op1.is_immediate || op1.is_string) && !op1.is_sysreg) has_imm = true;
    if ((op2.is_immediate || op2.is_string) && !op2.is_sysreg) has_imm = true;
    if (op1.has_offset || op2.has_offset) has_imm = true;   // LR/ST 指针偏移

    if (sr_params.has_sr) {
        uint32_t imm_bytes = (size_type != TOK_UNKNOWN) ? get_immediate_bytes(size_type) : 4;
        extra_bytes += 1 + imm_bytes; // k + offset
    } else if (has_imm) {
        uint32_t imm_bytes = (size_type != TOK_UNKNOWN) ? get_immediate_bytes(size_type) : 4;
        extra_bytes += imm_bytes;
    }

    uint32_t len_field = (extra_bytes > 0) ? (extra_bytes - 1) : 0;
    byte0 |= (len_field & 0x0F);

    result.bytes.push_back(byte0);

    // Byte1
    result.bytes.push_back(static_cast<uint8_t>(opcode & 0x7F));

    // Byte2
    if (has_operand_table) {
        uint8_t byte2 = 0;
        uint8_t op1_code = 0xE;
        uint8_t op2_code = 0xE;

        if (op1.is_register || op1.is_sysreg) {
            // 注意: parser 已把 sysreg token 转换为编码值存入 op1.value, 这里直接使用
            op1_code = static_cast<uint8_t>(op1.value & 0x0F);
        } else if (op1.is_immediate || op1.is_string) {
            op1_code = 0xF;
        }

        if (op2.is_register || op2.is_sysreg) {
            op2_code = static_cast<uint8_t>(op2.value & 0x0F);
        } else if (op2.is_immediate || op2.is_string) {
            op2_code = 0xF;
        }

        byte2 = (op1_code << 4) | op2_code;
        result.bytes.push_back(byte2);
    }

    // 立即数 / SR
    if (sr_params.has_sr) {
        result.bytes.push_back(static_cast<uint8_t>(sr_params.scale_pow & 0xFF));
        uint32_t imm = sr_params.offset_value;
        uint32_t imm_bytes = (size_type != TOK_UNKNOWN) ? get_immediate_bytes(size_type) : 4;
        for (uint32_t i = 0; i < imm_bytes; i++) {
            result.bytes.push_back(static_cast<uint8_t>((imm >> (i * 8)) & 0xFF));
        }
    } else if (op1.is_immediate && !op1.is_string) {
        uint32_t imm = op1.value;
        uint32_t imm_bytes = (size_type != TOK_UNKNOWN) ? get_immediate_bytes(size_type) : 4;
        for (uint32_t i = 0; i < imm_bytes; i++) {
            result.bytes.push_back(static_cast<uint8_t>((imm >> (i * 8)) & 0xFF));
        }
    } else if (op1.is_string) {
        for (unsigned char c : op1.string_bytes) result.bytes.push_back(c);
    } else if (op2.is_immediate && !op2.is_string) {
        uint32_t imm = op2.value;
        uint32_t imm_bytes = (size_type != TOK_UNKNOWN) ? get_immediate_bytes(size_type) : 4;
        for (uint32_t i = 0; i < imm_bytes; i++) {
            result.bytes.push_back(static_cast<uint8_t>((imm >> (i * 8)) & 0xFF));
        }
    } else if (op2.is_string) {
        for (unsigned char c : op2.string_bytes) result.bytes.push_back(c);
    } else if (op1.has_offset) {        // LR/ST 指针偏移立即数
        uint32_t imm = op1.offset_value;
        uint32_t imm_bytes = (size_type != TOK_UNKNOWN) ? get_immediate_bytes(size_type) : 4;
        for (uint32_t i = 0; i < imm_bytes; i++) {
            result.bytes.push_back(static_cast<uint8_t>((imm >> (i * 8)) & 0xFF));
        }
    } else if (op2.has_offset) {
        uint32_t imm = op2.offset_value;
        uint32_t imm_bytes = (size_type != TOK_UNKNOWN) ? get_immediate_bytes(size_type) : 4;
        for (uint32_t i = 0; i < imm_bytes; i++) {
            result.bytes.push_back(static_cast<uint8_t>((imm >> (i * 8)) & 0xFF));
        }
    }

    return result;
}

} // namespace dasm
