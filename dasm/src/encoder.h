#ifndef DASM_ENCODER_H
#define DASM_ENCODER_H

#include "token.h"
#include <vector>
#include <string>
#include <cstdint>

namespace dasm {

struct enc_operand_t {
    uint64_t value;
    uint64_t value_hi;   // ELDI 80 位立即数的高 16 位
    bool is_register;
    bool is_immediate;
    bool is_string;
    bool is_sysreg;
    bool has_offset;          // LR/ST 的 *reg+N 指针偏移
    uint32_t offset_value;    // 偏移量（表达式求值结果）
    std::vector<unsigned char> string_bytes;
};

struct enc_sr_params_t {
    bool has_sr;
    bool base_is_register;
    uint32_t base_value;
    bool index_is_register;
    uint32_t index_value;
    uint32_t scale_pow;
    bool offset_is_immediate;
    uint32_t offset_value;
};

struct enc_result_t {
    bool success;
    std::vector<unsigned char> bytes;
    std::string error_msg;
};

enc_result_t encoder_encode(token_type instr_type,
                            int opcode,
                            bool has_nz,
                            bool has_rep,
                            token_type size_type,
                            uint32_t size_bytes,
                            const enc_operand_t& op1,
                            const enc_operand_t& op2,
                            const enc_sr_params_t& sr_params);

uint32_t encoder_get_operand_encoding(token_type reg_type);

} // namespace dasm

#endif
