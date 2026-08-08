// decode.h - 解码 头文件
#ifndef DECODE_H
#define DECODE_H
#include "cpu.h"
typedef struct {
	Instr_index opcode;		// 操作码
	uint8_t op_size;	// 操作尺寸
	bool has_nz;		// NZ
	bool has_rep;		// REP
	uint8_t op1;		// 第一个操作数
	uint8_t op2;		// 第二个操作数
	uint32_t imm;		// 立即数
	
	uint8_t i_type;		// 指令类型，0=普通指令 1=SR 2=SETB/GETB
	uint8_t sr_k;		// SR命令的k值
} Decoded_instr;

int decode(DOCTOR_CPU *cpu, Decoded_instr *instr);
void instr_init(Decoded_instr *instr);
#endif

