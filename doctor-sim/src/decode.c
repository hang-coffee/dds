#include "decode.h"
#include <stdint.h>
#include <stdio.h>

// 从代码空间取一字节；越界返回 -1（不再依赖宿主 SIGSEGV 兜底）
static inline int fetch_byte(DOCTOR_CPU *cpu, uint32_t *pos, uint8_t *out) {
	if(*pos >= CODE_SIZE) return -1;
	*out = cpu->code_mem[(*pos)++];
	return 0;
}

void instr_init(Decoded_instr *instr) {
	instr->has_nz=0;
	instr->has_rep=0;
	instr->i_type=0;
	instr->imm=0;
	instr->imm_hi=0;
	instr->imm_hi2=0;
	instr->op1=0;
	instr->op2=0;
	instr->op_size=0;
	instr->opcode=0;
	instr->sr_k=0;
	return;
}

// 解码一条指令。返回值为指令长度（字节），-1 表示非法指令/越界（#II）。
// 不再使用文件级静态变量 P，可重入；位置完全由 cpu->P 驱动。
int decode(DOCTOR_CPU *cpu, Decoded_instr *instr) {
	uint32_t pos=cpu->P;
	instr->imm=0;
	instr->i_type=0;
	// 至少需要 2 字节
	if(pos+2>CODE_SIZE) return -1;
	// 解析 byte0
	uint8_t membyte=0;
	fetch_byte(cpu, &pos, &membyte);
	instr->has_rep=((membyte&0x80)!=0)?(true):(false);	// bit 7 --> rep
	instr->op_size=(membyte&0x60)>>5;					// 操作数尺寸
	instr->has_nz=((membyte&0x10)!=0)?(true):(false);	// bit 4 --> nz
	uint8_t needed_bytes=(membyte&0x0f);				// bit 3~0: 除Byte0外还需读取的字节数
	// 解析 byte1
	fetch_byte(cpu, &pos, &membyte);
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
				return (int)(pos-cpu->P);
			default:
				return -1;
		}
	}
	// 解析操作数表
	if(pos+1>CODE_SIZE) return -1;
	fetch_byte(cpu, &pos, &membyte);
	instr->op1=(membyte&0xf0)>>4;						// bit 7-4 --> op1
	instr->op2=(membyte&0x0f);							// bit 3-0 --> op2
	needed_bytes--;
	if(needed_bytes==0) return (int)(pos-cpu->P);
	// ELDI：80 位立即数（Byte2 后跟 10 字节 Little-Endian）
	if(instr->opcode==ELDI) {
		if(needed_bytes!=10 || pos+10>CODE_SIZE) return -1;
		instr->imm=0;
		instr->imm_hi=0;
		instr->imm_hi2=0;
		for(int i=0; i<4; i++) {
			fetch_byte(cpu, &pos, &membyte);
			instr->imm|=((uint32_t)membyte)<<(i*8);
		}
		for(int i=0; i<4; i++) {
			fetch_byte(cpu, &pos, &membyte);
			instr->imm_hi|=((uint32_t)membyte)<<(i*8);
		}
		for(int i=0; i<2; i++) {
			fetch_byte(cpu, &pos, &membyte);
			instr->imm_hi2|=((uint32_t)membyte)<<(i*8);
		}
		return (int)(pos-cpu->P);
	}
	if(instr->opcode!=SR && instr->op_size==0) return -1;					// #II
	// LR/ST 的 *reg+N 指针偏移：剩余字节即为偏移立即数（宽度=尺寸），
	// 与汇编器 `LR DWORD A, *R+4` / `ST DWORD *I+0x10, B` 的编码对应。
	// 偏移按尺寸符号扩展（BYTE: -128..127, WORD: -32768..32767, DWORD: 全范围），
	// 使 `*R-7` 之类的负偏移能正确回绕。
	if(instr->opcode==LR || instr->opcode==ST) {
		if(instr->op_size<1 || instr->op_size>3) return -1;		// 尺寸非法
		uint32_t imm_bytes=1u<<(instr->op_size-1);				// 1/2/4
		if(needed_bytes!=imm_bytes) return -1;					// 长度字段与尺寸不符
		if(pos+imm_bytes>CODE_SIZE) return -1;
		uint32_t num=0;
		for(uint32_t i=0; i<imm_bytes; i++) {
			fetch_byte(cpu, &pos, &membyte);
			num|=((uint32_t)membyte)<<(i*8);
		}
		if(instr->op_size==1 && (num&0x80)) num|=0xffffff00;		// BYTE 符号扩展
		else if(instr->op_size==2 && (num&0x8000)) num|=0xffff0000;	// WORD 符号扩展
		instr->imm=num;
		return (int)(pos-cpu->P);
	}
	// 解析下一字节
	if(pos+1>CODE_SIZE) return -1;
	fetch_byte(cpu, &pos, &membyte);
	if(instr->opcode==SR) {								// SR命令特殊处理
		instr->sr_k=membyte;
		if(pos+1>CODE_SIZE) return -1;
		fetch_byte(cpu, &pos, &membyte);
		needed_bytes--;
		instr->i_type=1;
		if(needed_bytes==0) return (int)(pos-cpu->P);
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
	if(pos+(uint32_t)imm_bytes>CODE_SIZE) return -1;
	uint8_t imm[4];
	imm[0]=membyte;
	for(int i=1; i<imm_bytes; i++) {
		fetch_byte(cpu, &pos, &imm[i]);
	}
	uint32_t num=0;
	for(int i=0; i<imm_bytes; i++) {
		num|=((uint32_t)imm[i])<<(i*8);
	}
	instr->imm=num;
	// DLDI：64 位立即数，再读取高 32 位
	if(instr->opcode==DLDI && instr->op_size==3) {
		if(pos+4>CODE_SIZE) return -1;
		uint32_t hi=0;
		for(int i=0; i<4; i++) {
			fetch_byte(cpu, &pos, &membyte);
			hi|=((uint32_t)membyte)<<(i*8);
		}
		instr->imm_hi=hi;
	}
	return (int)(pos-cpu->P);
}
