// cpu.h - CPU的定义

#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

#include "device.h"

// 内存大小
#define CODE_SIZE (1024*1024*16)
#define DATA_SIZE (1024*1024*16)

typedef enum {
	REG_A=0,
	REG_B,
	REG_C,
	REG_D1,
	REG_D2,
	REG_S,
	REG_T,
	REG_F,
	REG_E,
	REG_R,
	REG_X,
	REG_I,
	REG_COUNT
} Reg_index;	// 通用寄存器

typedef enum {
	LET=0, MOV, XCHG, LR, ST, ZERO,
	ADD, SUB, MUL, DIV, DIV_QWORD, CSI, CDI,
	SHL, SHR, MSL, MSR, AND, OR, XOR, NEG, MNE,
	PUSH, POP, SFA, RER,
	PUSHR, POPR, SRA, SRB, LOD, STO, SR, 
	TEST, CMP, JMP, JZ, JNZ, JRZ, JRNZ, JA, JNA, JB, JNB, JG, JNG, JL, JNL,
	IN, OUT, INT, PUSH_RIN1, PUSH_RIN2, POP_RIN1, POP_RIN2, PUSHI, POPI, HLT,
	BLKS, PUSH_P, NOP, INC, DEC, BLKIN,
	SVC, IRET, SETB, GETB,
	POR, FMOV, FLDI, FLD, FST, FADD, FSUB, FMUL, FDIV,
	FSQRT, FNEG, FABS, FCMP, F2I, I2F, FPUSH, FPOP,
	DMOV, DLDI, DLD, DST, DADD, DSUB, DMUL, DDIV,
	DSQRT, DNEG, DABS, DCMP, D2I, I2D, DPUSH, DPOP,
	F2D, D2F
} Instr_index;	// 指令集

typedef struct {
	uint32_t cbase;
	uint32_t climit;
	uint32_t dbase;
	uint32_t dlimit;
	uint32_t ksp;
	uint32_t xar;
} Sys_regs;

typedef struct {
	uint32_t rin1;
	uint32_t rin2;
	uint32_t ictb;
	uint32_t ctrl;

	int current_int;
	uint32_t irq_base;
	bool intr_enable;
	bool inl;
	bool inside_ppi;		// 是否在PUSHI/POPI对中
	int exception;
	uint8_t cpl;
}Intr_ctrl;

typedef struct DOCTOR_CPU_t{
	uint32_t regs[REG_COUNT];
	uint32_t fp_regs[8];	// DFE: FP0-FP7
	double dbl_regs[8];	// DFE: DP0-DP7（64 位 double）
	uint32_t fpcr;		// DFE: FPCR
	
	uint32_t P;

	Sys_regs sys;	// 系统寄存器
	Intr_ctrl intr;

	uint8_t *code_mem;
	uint8_t *data_mem;

	bool halted;

	Device_mgr dev_mgr;
} DOCTOR_CPU;

typedef enum {
	ERR_DIV=0,
	ERR_II,
	ERR_STACK,
	ERR_GP,
	ERR_NMI,
} Exceptions;

void cpu_init(DOCTOR_CPU *cpu);
int cpu_load_bin(DOCTOR_CPU *cpu, const char *filename);		// 加载代码镜像, 返回0=成功, -1=失败
int cpu_load_data_bin(DOCTOR_CPU *cpu, const char *filename);	// 加载数据镜像, 返回0=成功, -1=失败
void cpu_run(DOCTOR_CPU *cpu);
void cpu_free(DOCTOR_CPU *cpu);

#include <signal.h>
extern volatile sig_atomic_t sigint_received;

#endif
