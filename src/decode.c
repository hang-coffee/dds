#include "decode.h"
#include <stdint.h>
#include <stdio.h>

static uint32_t P=0;

static inline uint8_t get_next_byte(DOCTOR_CPU *cpu) {
//	fprintf(stderr, "\033[32mP=%x\033[0m", P);
	uint8_t ret=cpu->code_mem[P];
	P++;
	return ret;
}

void instr_init(Decoded_instr *instr) {
	instr->has_nz=0;
	instr->has_rep=0;
	instr->i_type=0;
	instr->imm=0;
	instr->op1=0;
	instr->op2=0;
	instr->op_size=0;
	instr->opcode=0;
	instr->sr_k=0;
	return;
}

int decode(DOCTOR_CPU *cpu, Decoded_instr *instr) {
	P=cpu->P;
	instr->imm=0;
	instr->i_type=0;
	// 解析 byte0
	uint8_t membyte=get_next_byte(cpu);
	instr->has_rep=((membyte&0x80)!=0)?(true):(false);	// bit 7 --> rep
	instr->op_size=(membyte&0x60)>>5;					// 操作数尺寸
	instr->has_nz=((membyte&0x10)!=0)?(true):(false);	// bit 4 --> nz
	uint8_t needed_bytes=(membyte&0x0f);				// bit 3~0
//	fprintf(stderr, "total bytes=%u\n", needed_bytes);
	// 解析 byte1
	membyte=get_next_byte(cpu);
	instr->opcode=(membyte&0x7f);						// 最高位保留
	// 根据needed_byte进一步解析
	if(needed_bytes==0) {								// 如果是两字节命令
		switch(instr->opcode) {
			case DIV_QWORD:
			case CSI:
			case CDI:
			case RER:
			case PUSHR:
			case POPR:
			case SRA:
			case SRB:
			case JMP:
			case JZ:
			case JNZ:
			case PUSH_RIN1:
			case PUSH_RIN2:
			case POP_RIN1:
			case POP_RIN2:
			case PUSHI:
			case POPI:
			case HLT:
			case NOP:
			case SVC:
			case IRET:
				return P-cpu->P;
			default:
				return -1;
		}
	}
	// 解析操作数表
	membyte=get_next_byte(cpu);
	instr->op1=(membyte&0xf0)>>4;						// bit 7-4 --> op1
	instr->op2=(membyte&0x0f);							// bit 3-0 --> op2
	needed_bytes--;
	// 解析下一字节
	if(needed_bytes==0) return P-cpu->P;
	if(instr->opcode!=SR && instr->op_size==0) return -1;					// #II
	membyte=get_next_byte(cpu);
	if(instr->opcode==SR) {								// SR命令特殊处理
		instr->sr_k=membyte;
		membyte=get_next_byte(cpu);
		needed_bytes--;
		instr->i_type=1;
//		fprintf(stderr, "INFO: %02X\n", membyte);
		if(needed_bytes==0) return P-cpu->P;
	}
	// 解析立即数，这里是Little Endian!
	bool im=false;
	if(instr->op1==0xf) im=true;
	if(instr->op2==0xf) im=true;
	if(instr->opcode==SR) im=true;
	if(!im) return -1;
	int imm_bytes=0;
	switch(instr->op_size) {
		case 1:											// byte
			imm_bytes=1;
			break;
		case 2:
			imm_bytes=2;
			break;
		case 3:
			imm_bytes=4;
			break;
		default:
			return -1;									// #II, 因为指定了多余的读取数量
	}
	uint8_t imm[4];
	imm[0]=membyte;
	for(int i=1; i<imm_bytes; i++) {
		imm[i]=get_next_byte(cpu);
//		fprintf(stderr, "i=%d, BYTE=%X\t\t", i, imm[i]);
	}
	uint32_t num=0;
	for(int i=0; i<imm_bytes; i++) {
		num|=((uint32_t)imm[i])<<(i*8);
	}
	instr->imm=num;
	return P-cpu->P;
}

