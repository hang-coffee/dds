#ifndef MEM_H
#define MEM_H
#include <stdio.h>
#include "cpu.h"

static inline uint32_t *op2reg(DOCTOR_CPU *cpu, Reg_index reg) {
	return &(cpu->regs[reg]);
}

#define MEM_TYPE_CODE 0
#define MEM_TYPE_DATA 1

// 物理内存范围检查: [addr, addr+bytes) 是否完全落在对应空间内。
// 所有 load/store 内部都做此防护（越界返回 0 / 忽略写入），
// 不再越界访问宿主内存（原依赖 SIGSEGV + siglongjmp 兜底）。
static inline bool mem_range_ok(DOCTOR_CPU *cpu, uint32_t addr, uint32_t bytes, uint8_t mem_type) {
	(void)cpu;
	uint32_t size=(mem_type==MEM_TYPE_CODE)?(CODE_SIZE):(DATA_SIZE);
	if(bytes==0) return addr < size;			// 单点: 左闭右开 [0, size)
	return ((uint64_t)addr + bytes <= size);
}

static inline uint8_t load_from_mem(DOCTOR_CPU *cpu, uint32_t ptr, uint8_t mem_type) {
	uint8_t ret=0;
	if(!mem_range_ok(cpu, ptr, 1, mem_type)) return 0;		// 越界防护
	if(mem_type==MEM_TYPE_CODE) ret=cpu->code_mem[ptr];
	if(mem_type==MEM_TYPE_DATA) ret=cpu->data_mem[ptr];
	return ret;
}

static inline uint16_t load_word_from_mem(DOCTOR_CPU *cpu, uint32_t ptr, uint8_t mem_type) {
	uint16_t ret=0;
	if(!mem_range_ok(cpu, ptr, 2, mem_type)) return 0;		// 越界防护
	if(mem_type==MEM_TYPE_CODE) {
		ret=cpu->code_mem[ptr];
		ret|=((cpu->code_mem[ptr+1])<<8);
	}
	if(mem_type==MEM_TYPE_DATA) {
		ret=cpu->data_mem[ptr];
		ret|=((cpu->data_mem[ptr+1])<<8);
	}
	return ret;
}

static inline uint32_t load_dword_from_mem(DOCTOR_CPU *cpu, uint32_t ptr, uint8_t mem_type) {
	uint32_t ret=0;
	if(!mem_range_ok(cpu, ptr, 4, mem_type)) return 0;		// 越界防护
	if(mem_type==MEM_TYPE_CODE) {
		ret=cpu->code_mem[ptr];
		ret|=((cpu->code_mem[ptr+1])<<8);
		ret|=((cpu->code_mem[ptr+2])<<16);
		ret|=((cpu->code_mem[ptr+3])<<24);
	}
	if(mem_type==MEM_TYPE_DATA) {
		ret=cpu->data_mem[ptr];
		ret|=((cpu->data_mem[ptr+1])<<8);
		ret|=((cpu->data_mem[ptr+2])<<16);
		ret|=((cpu->data_mem[ptr+3])<<24);
	}
	return ret;
}

static inline void set_mem(DOCTOR_CPU *cpu, uint32_t ptr, uint8_t num, uint8_t mem_type) {
	// 越界防护：忽略写入（调用方负责在指令级报 #GP/#STACK）
	if(!mem_range_ok(cpu, ptr, 1, mem_type)) return;
	if(mem_type==MEM_TYPE_DATA) {
		cpu->data_mem[ptr]=num;
	} else {
		cpu->code_mem[ptr]=num;
	}
	return;
}

static inline void set_word_mem(DOCTOR_CPU *cpu, uint32_t ptr, uint32_t num, uint8_t mem_type) {
	set_mem(cpu, ptr, (uint8_t)((num&0x000000ff)), mem_type);
	set_mem(cpu, ptr+1, (uint8_t)((num&0x0000ff00)>>8), mem_type);
}

static inline void set_dword_mem(DOCTOR_CPU *cpu, uint32_t ptr, uint32_t num, uint8_t mem_type) {
	set_mem(cpu, ptr, (uint8_t)((num&0x000000ff)), mem_type);
	set_mem(cpu, ptr+1, (uint8_t)((num&0x0000ff00)>>8), mem_type);
	set_mem(cpu, ptr+2, (uint8_t)((num&0x00ff0000)>>16), mem_type);
	set_mem(cpu, ptr+3, (uint8_t)((num&0xff000000)>>24), mem_type);
}

static inline int push(DOCTOR_CPU *cpu, uint32_t num, uint8_t size) { // 返回值为错误
	// 栈上溢预检：S+实际字节数 超出数据空间则不写入（调用方负责报 #STACK）
	if(size<1 || size>3) return 1;
	uint32_t bytes=1u<<(size-1);	// BYTE=1, WORD=2, DWORD=4
	if((uint64_t)(*op2reg(cpu, REG_S))+bytes > DATA_SIZE) return 2;
	switch(size) {
		case 1:
			(*op2reg(cpu, REG_S))++;
			cpu->data_mem[(*op2reg(cpu, REG_S))]=(uint8_t)(num&0xff);
			break;
		case 2:
			(*op2reg(cpu, REG_S))++;
			cpu->data_mem[(*op2reg(cpu, REG_S))]=(uint8_t)(num&0x00ff);
			(*op2reg(cpu, REG_S))++;
			cpu->data_mem[(*op2reg(cpu, REG_S))]=(uint8_t)((num&0xff00)>>8);
			break;
		case 3:
			(*op2reg(cpu, REG_S))++;
			cpu->data_mem[(*op2reg(cpu, REG_S))]=(uint8_t)(num&0x000000ff);
			(*op2reg(cpu, REG_S))++;
			cpu->data_mem[(*op2reg(cpu, REG_S))]=(uint8_t)((num&0x0000ff00)>>8);
			(*op2reg(cpu, REG_S))++;
			cpu->data_mem[(*op2reg(cpu, REG_S))]=(uint8_t)((num&0x00ff0000)>>16);
			(*op2reg(cpu, REG_S))++;
			cpu->data_mem[(*op2reg(cpu, REG_S))]=(uint8_t)((num&0xff000000)>>24);
			break;
		default:
			return 1;
	}
	return 0;
}

static inline uint32_t pop(DOCTOR_CPU *cpu, uint8_t size) {	// 返回值为弹出的值
	uint32_t res=0;
	// 栈下溢预检：S<实际字节数 时不读取（调用方负责报 #STACK）
	if(size<1 || size>3) return 0;
	uint32_t bytes=1u<<(size-1);
	if((*op2reg(cpu, REG_S))<bytes) return 0;
	switch(size) {
		case 1:
			res=load_from_mem(cpu, (*op2reg(cpu, REG_S)), MEM_TYPE_DATA);
			(*op2reg(cpu, REG_S))--;
			break;
		case 2:
			res=(load_from_mem(cpu, (*op2reg(cpu, REG_S)), MEM_TYPE_DATA)<<8);
			res|=load_from_mem(cpu, (*op2reg(cpu, REG_S)-1), MEM_TYPE_DATA);
			(*op2reg(cpu, REG_S))-=2;
			break;
		case 3:
			res=(load_from_mem(cpu, (*op2reg(cpu, REG_S)), MEM_TYPE_DATA)<<24);
			res|=(load_from_mem(cpu, (*op2reg(cpu, REG_S)-1), MEM_TYPE_DATA)<<16);
			res|=(load_from_mem(cpu, (*op2reg(cpu, REG_S)-2), MEM_TYPE_DATA)<<8);
			res|=load_from_mem(cpu, (*op2reg(cpu, REG_S)-3), MEM_TYPE_DATA);
			(*op2reg(cpu, REG_S))-=4;
			break;
		default:
			break;
	}
//	fprintf(stderr, "INFO: POP: RES=0x%08X\n", res);
	return res;	
}

#endif
