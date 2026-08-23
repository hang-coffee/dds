#include "execution.h"
#include "mem.h"
#include "debugger.h"
#include "interrupt.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// ============ 内存访问检查（物理边界 + MPU 区间） ============
// 物理边界（[0, DATA_SIZE) / [0, CODE_SIZE)）总是检查，越界 → #GP，
// 不再越界访问宿主内存；CTRL bit29 (MPU)=1 时再检查 [DBASE, DLIMIT) /
// [CBASE, CLIMIT)。异常/NMI/SVC 处理期间 MPU 强制关闭（ISR_NMO=0）。

static inline bool mpu_enabled(DOCTOR_CPU *cpu) {
	return CTRL_GET_MPU(cpu->intr.ctrl)!=0;
}

// 数据访问检查：[addr, addr+bytes) 必须在物理范围且（MPU=1 时）⊆ [DBASE, DLIMIT)。
// bytes=0 时做单点检查。返回 0=允许, 4=越界(#GP)
static inline int mem_check_data(DOCTOR_CPU *cpu, uint32_t addr, uint32_t bytes) {
	if(!mem_range_ok(cpu, addr, bytes, MEM_TYPE_DATA)) return 4;	// 物理越界
	if(!mpu_enabled(cpu)) return 0;
	if(bytes==0) {
		return (addr>=cpu->sys.dbase && addr<cpu->sys.dlimit) ? 0 : 4;
	}
	if(addr>=cpu->sys.dbase && (uint64_t)addr+bytes <= (uint64_t)cpu->sys.dlimit) return 0;
	return 4;
}

// 代码访问检查：[addr, addr+bytes) 必须在物理范围且（MPU=1 时）⊆ [CBASE, CLIMIT)。
// 返回 0=允许, 4=越界(#GP)
static inline int mem_check_code(DOCTOR_CPU *cpu, uint32_t addr, uint32_t bytes) {
	if(!mem_range_ok(cpu, addr, bytes, MEM_TYPE_CODE)) return 4;	// 物理越界
	if(!mpu_enabled(cpu)) return 0;
	if(bytes==0) {
		return (addr>=cpu->sys.cbase && addr<cpu->sys.climit) ? 0 : 4;
	}
	if(addr>=cpu->sys.cbase && (uint64_t)addr+bytes <= (uint64_t)cpu->sys.climit) return 0;
	return 4;
}

// 栈指针越界 → #STACK 的 XAR 值（bit31: 0=上溢(超出上界), 1=下溢(低于DBASE); 低31位=S新值）
static inline uint32_t mpu_stack_xar(DOCTOR_CPU *cpu, uint32_t s_new) {
	if(s_new < cpu->sys.dbase) return 0x80000000u | (s_new & 0x7fffffff);
	return s_new & 0x7fffffff;
}

// DFE 浮点位模式转换
static inline float fp_bits_to_float(uint32_t bits) {
float f;
memcpy(&f, &bits, sizeof(f));
return f;
}

static inline uint32_t fp_float_to_bits(float f) {
uint32_t bits;
memcpy(&bits, &f, sizeof(bits));
return bits;
}


static inline double dbl_from_mem(DOCTOR_CPU *cpu, uint32_t addr, uint8_t mem_type) {
uint32_t lo=load_dword_from_mem(cpu, addr, mem_type);
uint32_t hi=load_dword_from_mem(cpu, addr+4, mem_type);
uint64_t bits=((uint64_t)hi<<32)|lo;
double d;
memcpy(&d, &bits, sizeof(d));
return d;
}

static inline void dbl_to_mem(DOCTOR_CPU *cpu, uint32_t addr, double d, uint8_t mem_type) {
uint64_t bits;
memcpy(&bits, &d, sizeof(bits));
set_dword_mem(cpu, addr, (uint32_t)(bits & 0xffffffffu), mem_type);
set_dword_mem(cpu, addr+4, (uint32_t)(bits>>32), mem_type);
}

// DXE 80 位扩展精度转换
static inline long double ext_from_bits(const uint8_t b[10]) {
uint64_t mant=0;
for(int i=0; i<8; i++) mant |= ((uint64_t)b[i]) << (8*i);
uint16_t exp=(uint16_t)(((uint16_t)(b[9]&0x7f)<<8) | b[8]);
int sign=(b[9]&0x80)?1:0;
if(exp==0x7fff) {
if(mant==0x8000000000000000ULL)
return sign ? -INFINITY : INFINITY;
return sign ? -(long double)NAN : (long double)NAN;
}
if(exp==0 && mant==0)
return sign ? -0.0L : 0.0L;
int e=(exp==0)?1:(int)exp;
long double val=ldexpl((long double)mant, e-16383-63);
return sign ? -val : val;
}

static inline void ext_to_bits(long double x, uint8_t b[10]) {
memset(b, 0, 10);
int sign=signbit(x)?1:0;
uint16_t exp=0;
uint64_t mant=0;
if(isnan(x)) {
exp=0x7fff;
mant=0xc000000000000000ULL;
} else if(isinf(x)) {
exp=0x7fff;
mant=0x8000000000000000ULL;
} else if(x==0.0L) {
b[9]=(uint8_t)(sign?0x80:0);
return;
} else {
int e=0;
long double m=frexpl(x, &e);
m=fabsl(m);
long double scaled=ldexpl(m, 64);
if(scaled >= 18446744073709551616.0L) mant=0xFFFFFFFFFFFFFFFFULL;
else mant=(uint64_t)scaled;
if(mant==0) mant=1;
exp=(uint16_t)(e + 16382);
}
b[8]=(uint8_t)(exp & 0xff);
b[9]=(uint8_t)(((exp>>8)&0x7f) | (sign?0x80:0));
for(int i=0; i<8; i++) b[i]=(uint8_t)((mant>>(8*i))&0xff);
}

static inline long double ext_from_mem(DOCTOR_CPU *cpu, uint32_t addr, uint8_t mem_type) {
uint8_t b[10];
for(int i=0; i<10; i++) b[i]=load_from_mem(cpu, addr+i, mem_type);
return ext_from_bits(b);
}

static inline void ext_to_mem(DOCTOR_CPU *cpu, uint32_t addr, long double v, uint8_t mem_type) {
uint8_t b[10];
ext_to_bits(v, b);
for(int i=0; i<10; i++) set_mem(cpu, addr+i, b[i], mem_type);
}

// 跳转：设置 P=E。返回 0=成功；4=跳转目标越界（#GP）
static inline int jmp(DOCTOR_CPU *cpu) {
	uint32_t t=*op2reg(cpu, REG_E);
	if(mem_check_code(cpu, t, 0)) return 4;	// 物理 + MPU: E 必须在代码范围
	cpu->P=t;
	return 0;
}

int execute(DOCTOR_CPU *cpu, Decoded_instr *instr) {
//	fprintf(stderr, "INFO: P=0x%08X, INSTR=%u %u %u, %u (imm=%08X)\n", 
//			cpu->P, instr->opcode, instr->op_size, instr->op1, instr->op2, instr->imm);
	int err=0;
	uint32_t res=0;
	switch(instr->opcode) {
		case LET:
			if(instr->op2!=0xf) {
				exe_err(cpu);
				err=1;
			} else {
				if(instr->has_nz) {
					// NZ: 高位保留，仅替换低 N 位（按尺寸）
					uint32_t dest=*op2reg(cpu, instr->op1);
					switch(instr->op_size) {
						case 1:
							res=(dest&0xffffff00)|(instr->imm&0xff);
							break;
						case 2:
							res=(dest&0xffff0000)|(instr->imm&0xffff);
							break;
						case 3:
							res=instr->imm;
							break;
						default:
							exe_err(cpu);
							err=1;
							return err;
					}
				} else res=instr->imm;
				// MPU: 向 E/S/F 写指针时检查新值所在区间
				if(instr->op1==REG_E) {
					if(mem_check_code(cpu, res, 0)) { exe_err(cpu); return 4; }	// #GP
				} else if(instr->op1==REG_S || instr->op1==REG_F) {
					if(mem_check_data(cpu, res, 0)) { exe_err(cpu); cpu->sys.xar=mpu_stack_xar(cpu, res); return 3; }	// #STACK
				}
				*op2reg(cpu, instr->op1)=res;
				err=0;
			}
			break;
		case MOV:
			if(instr->op1==0xe || instr->op2==0xe || instr->op1==0xf || instr->op2==0xf) {
				exe_err(cpu);
				err=1;
			} else {
				if(instr->has_nz) {
					// NZ: 高位保留，仅替换低 N 位（按尺寸）
					uint32_t dest=*op2reg(cpu, instr->op1);
					switch(instr->op_size) {
						case 1:
							res=(dest&0xffffff00)|((*op2reg(cpu, instr->op2))&0xff);
							break;
						case 2:
							res=(dest&0xffff0000)|((*op2reg(cpu, instr->op2))&0xffff);
							break;
						case 0:
						case 3:
							res=*op2reg(cpu, instr->op2);
							break;
						default:
							exe_err(cpu);
							err=1;
							return err;
					}
				} else {
					res=*op2reg(cpu, instr->op2);
					switch(instr->op_size) {
						case 0:							// 未指定尺寸: 完整DWORD
							break;
						case 1:
							res&=0x000000ff;
							break;
						case 2:
							res&=0x0000ffff;
							break;
						case 3:
							break;
						default:
							exe_err(cpu);
							err=1;
							return err;
					}
				}
				// MPU: 向 E/S/F 搬运指针时检查新值所在区间
				if(instr->op1==REG_E) {
					if(mem_check_code(cpu, res, 0)) { exe_err(cpu); return 4; }	// #GP
				} else if(instr->op1==REG_S || instr->op1==REG_F) {
					if(mem_check_data(cpu, res, 0)) { exe_err(cpu); cpu->sys.xar=mpu_stack_xar(cpu, res); return 3; }	// #STACK
				}
				*op2reg(cpu, instr->op1)=res;
				err=0;
			}
			break;
		case XCHG:
			if(instr->op1==0xe||instr->op1==0xf||instr->op2==0xe||instr->op2==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			// 注：REP 的重复语义在 cpu_run() 中统一实现（以 C 为计数器），
			// 这里不再对 XCHG 做特殊处理
			uint32_t ne=*op2reg(cpu, instr->op2);
			*op2reg(cpu, instr->op2)=*op2reg(cpu, instr->op1);
			*op2reg(cpu, instr->op1)=ne;
			break;
		case LR:
			if(instr->op1==0xe || instr->op1==0xf || instr->op2==REG_C || instr->op2==REG_D1 || instr->op2==REG_D2 || instr->op2==REG_X) {
				exe_err(cpu);
				err=1;
				return err;
			}
			int reg=*op2reg(cpu, instr->op2)+instr->imm;
			// MPU: 数据指针解引用检查（*E 读代码空间，manual 未列入检查）
			if(instr->op2!=REG_E && instr->op_size>=1 && instr->op_size<=3) {
				if(mem_check_data(cpu, (uint32_t)reg, 1u<<(instr->op_size-1))) {
					exe_err(cpu);
					return 4;		// #GP
				}
			}
			if(instr->has_nz) {
				res=*op2reg(cpu, instr->op1);
				switch(instr->op_size) {
					case 1:
						res&=0xffffff00;
						uint8_t unit=load_from_mem(cpu, reg, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
						res+=unit;
						break;
					case 2:
						res&=0xffff0000;
						uint16_t unit16=(load_from_mem(cpu, reg, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA)))
							| (load_from_mem(cpu, reg+1, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA))<<8);
						res+=unit16;
						break;
					case 3:
						res=(load_from_mem(cpu, reg, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA)))
							| (load_from_mem(cpu, reg+1, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA))<<8)
							| (load_from_mem(cpu, reg+2, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA))<<16)
							| (load_from_mem(cpu, reg+3, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA))<<24);
						break;
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
			} else {
				switch(instr->op_size) {
					case 1:
						res=load_from_mem(cpu, reg, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
						break;
					case 2:
						res=(load_from_mem(cpu, reg, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA)))
							| (load_from_mem(cpu, reg+1, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA))<<8);
						break;
					case 3:
						res=(load_from_mem(cpu, reg, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA)))
							| (load_from_mem(cpu, reg+1, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA))<<8)
							| (load_from_mem(cpu, reg+2, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA))<<16)
							| (load_from_mem(cpu, reg+3, ((instr->op2)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA))<<24);
						break;
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
			}
			*op2reg(cpu, instr->op1)=res;
			break;
		case ST:
			if(instr->op1==0xe||instr->op2==0xe||instr->op1==0xf||instr->op2==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			uint32_t des=(*op2reg(cpu, instr->op1))+instr->imm;
			// MPU: 数据指针解引用检查（*E 写代码空间，manual 未列入检查）
			if(instr->op1!=REG_E && instr->op_size>=1 && instr->op_size<=3) {
				if(mem_check_data(cpu, des, 1u<<(instr->op_size-1))) {
					exe_err(cpu);
					return 4;		// #GP
				}
			}
			switch(instr->op_size) {
				case 1:
					set_mem(cpu, des, ((*op2reg(cpu, (instr->op2)))&0xff), ((instr->op1)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
					break;
				case 2:
					set_mem(cpu, des, ((*op2reg(cpu, (instr->op2)))&0x00ff), ((instr->op1)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
					set_mem(cpu, des+1, (((*op2reg(cpu, (instr->op2)))&0xff00)>>8), ((instr->op1)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
					break;
				case 3:
					set_mem(cpu, des, ((*op2reg(cpu, (instr->op2)))&0x000000ff), ((instr->op1)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
					set_mem(cpu, des+1, ((*op2reg(cpu, (instr->op2)))&0x0000ff00)>>8, ((instr->op1)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
					set_mem(cpu, des+2, ((*op2reg(cpu, (instr->op2)))&0x00ff0000)>>16, ((instr->op1)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
					set_mem(cpu, des+3, ((*op2reg(cpu, (instr->op2)))&0xff000000)>>24, ((instr->op1)==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			break;
		case ZERO:
			if(instr->op1==0xe) {
				exe_err(cpu);
				err=1;
				return err;
			}
			*op2reg(cpu, instr->op1)=0;
			break;
		case ADD:
			if(instr->op1==0xe || instr->op2==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			uint32_t nval=(*op2reg(cpu, instr->op1));
			uint32_t js=0;
			if(instr->op2==0xf) js=instr->imm;
			else js=(*op2reg(cpu, instr->op2));
			if(instr->has_nz) {
				// NZ: 高位保留，低 N 位相加（按尺寸回绕）
				switch(instr->op_size) {
					case 1:
						res=(nval&0xffffff00)|((nval+js)&0xff);
						break;
					case 2:
						res=(nval&0xffff0000)|((nval+js)&0xffff);
						break;
					case 3:
						res=nval+js;
						break;
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
				*op2reg(cpu, instr->op1)=res;
			} else {
				switch(instr->op_size) {
					case 1:
						nval&=0xff;
						js&=0xff;
						nval+=js;
						nval&=0xff;
						break;
					case 2:
						nval&=0xffff;
						js&=0xffff;
						nval+=js;
						nval&=0xffff;
						break;
					case 3:
						nval+=js;
						break;
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
				*op2reg(cpu, instr->op1)=nval;
			}
			break;
		case SUB:
			if(instr->op1==0xe || instr->op2==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			uint32_t nvals=(*op2reg(cpu, instr->op1));
			uint32_t jss=0;
			if(instr->op2==0xf) jss=-((uint32_t)instr->imm);
			else jss=-((uint32_t)*op2reg(cpu, instr->op2));
			if(instr->has_nz) {
				// NZ: 高位保留，低 N 位相减（按尺寸回绕）
				switch(instr->op_size) {
					case 1:
						res=(nvals&0xffffff00)|((nvals+jss)&0xff);
						break;
					case 2:
						res=(nvals&0xffff0000)|((nvals+jss)&0xffff);
						break;
					case 3:
						res=nvals+jss;
						break;
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
				*op2reg(cpu, instr->op1)=res;
			} else {
				switch(instr->op_size) {
					case 1:
						nvals&=0xff;
						jss&=0xff;
						nvals+=jss;
						nvals&=0xff;
						break;
					case 2:
						nvals&=0xffff;
						jss&=0xffff;
						nvals+=jss;
						nvals&=0xffff;
						break;
					case 3:
						nvals+=jss;
						break;
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
				*op2reg(cpu, instr->op1)=nvals;
			}
			break;
		case MUL:
			if(instr->op1==0xe || instr->op1==0xf || instr->op2==0xe || instr->op2==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			switch(instr->op_size) {
				case 1:
					uint16_t mul16=(uint16_t)((uint8_t)(*op2reg(cpu, instr->op1)));
					mul16*=(uint8_t)(*op2reg(cpu, instr->op2));
					res=(uint32_t)mul16;
					*op2reg(cpu, REG_D2)=res;
					break;
				case 2:
					uint32_t mul32=(uint32_t)((uint16_t)(*op2reg(cpu, instr->op1)));
					mul32*=(uint16_t)(*op2reg(cpu, instr->op2));
					res=mul32;
					*op2reg(cpu, REG_D2)=res;
					break;
				case 3:
					uint64_t mul64=(uint64_t)*op2reg(cpu, instr->op1);
					mul64*=(*op2reg(cpu, instr->op2));
					*op2reg(cpu, REG_D1)=(mul64&0xffffffff00000000)>>32;
					*op2reg(cpu, REG_D2)=(mul64&0x00000000ffffffff);
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			break;
		case DIV:
			if(instr->op1==0xe||instr->op1==0xf||instr->op2==0xe||instr->op2==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if(*op2reg(cpu, instr->op2)==0) {
				exe_err(cpu);
				err=2;
				return err;
			}
			uint32_t div_s=0, div_y=0;
			switch(instr->op_size) {
				case 1:
					div_s=(*op2reg(cpu, instr->op1)&0xff)/(*op2reg(cpu, instr->op2)&0xff);
					div_y=(*op2reg(cpu, instr->op1)&0xff)%(*op2reg(cpu, instr->op2)&0xff);
					break;
				case 2:
					div_s=(*op2reg(cpu, instr->op1)&0xffff)/(*op2reg(cpu, instr->op2)&0xffff);
					div_y=(*op2reg(cpu, instr->op1)&0xffff)%(*op2reg(cpu, instr->op2)&0xffff);
					break;
				case 3:
					div_s=(*op2reg(cpu, instr->op1))/(*op2reg(cpu, instr->op2));
					div_y=(*op2reg(cpu, instr->op1))%(*op2reg(cpu, instr->op2));
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			*op2reg(cpu, REG_D1)=div_y;
			*op2reg(cpu, REG_D2)=div_s;
			break;
		case DIV_QWORD:
			uint64_t dq_1, dq_2;
			dq_1=((uint64_t)(*op2reg(cpu, REG_D1))<<32)+(*op2reg(cpu, REG_D2));
			dq_2=((uint64_t)(*op2reg(cpu, REG_A))<<32)+(*op2reg(cpu, REG_B));
			if(dq_2==0) {
				exe_err(cpu);
				err=2;
				return err;
			}
			*op2reg(cpu, REG_D2)=dq_1/dq_2;
			*op2reg(cpu, REG_D1)=dq_1%dq_2;
			break;
		case CSI:
			(*op2reg(cpu, REG_C))++;
			break;
		case CDI:
			(*op2reg(cpu, REG_C))--;
			break;
		case SHL:
		case MSL:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			// 移位数与结果均按尺寸截断（BYTE/WORD/DWORD）
			{
				uint32_t sv=*op2reg(cpu, instr->op1);
				switch(instr->op_size) {
					case 1:
						sv=(sv&0xff)<<(instr->imm&0x07);
						sv&=0xff;
						break;
					case 2:
						sv=(sv&0xffff)<<(instr->imm&0x0f);
						sv&=0xffff;
						break;
					case 3:
						sv<<=(instr->imm&0x1f);
						break;
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
				*op2reg(cpu, instr->op1)=sv;
			}
			break;
		case SHR:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			{
				uint32_t sv=*op2reg(cpu, instr->op1);
				switch(instr->op_size) {
					case 1:
						sv=(sv&0xff)>>(instr->imm&0x07);
						break;
					case 2:
						sv=(sv&0xffff)>>(instr->imm&0x0f);
						break;
					case 3:
						sv>>=(instr->imm&0x1f);
						break;
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
				*op2reg(cpu, instr->op1)=sv;
			}
			break;
		case MSR:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			// 算术右移：按尺寸取符号位并扩展
			{
				uint32_t sv=*op2reg(cpu, instr->op1);
				switch(instr->op_size) {
					case 1: {
						uint32_t sign=sv&0x80;
						uint32_t n=instr->imm&0x07;
						uint8_t v8=(uint8_t)(sv&0xff);
						v8=(uint8_t)(v8>>n);
						if(sign && n>0) v8|=(uint8_t)(0xff<<(8-n));
						sv=(uint32_t)v8;
						break;
					}
					case 2: {
						uint32_t sign=sv&0x8000;
						uint32_t n=instr->imm&0x0f;
						uint16_t v16=(uint16_t)(sv&0xffff);
						v16=(uint16_t)(v16>>n);
						if(sign && n>0) v16|=(uint16_t)(0xffff<<(16-n));
						sv=(uint32_t)v16;
						break;
					}
					case 3: {
						uint32_t sign=sv&0x80000000;
						uint32_t n=instr->imm&0x1f;
						sv>>=n;
						if(sign && n>0) sv|=(0xffffffff<<(32-n));
						break;
					}
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
				*op2reg(cpu, instr->op1)=sv;
			}
			break;
		case AND:
			if(instr->op1==0xe || instr->op1==0xf || instr->op2==0xe || instr->op2==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1))&(*op2reg(cpu, instr->op2));
			uint32_t mask=0;
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					break;
				case 2:
					mask=0xffff;
					break;
				case 3:
					mask=0xffffffff;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			res&=mask;
			if(instr->has_nz) {
				(*op2reg(cpu, instr->op1))&=(~mask);
				(*op2reg(cpu, instr->op1))|=res;
			} else {
				(*op2reg(cpu, instr->op1))=res;
			}
			break;
		case OR:
			if(instr->op1==0xe || instr->op1==0xf || instr->op2==0xe || instr->op2==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1))|(*op2reg(cpu, instr->op2));
			mask=0;
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					break;
				case 2:
					mask=0xffff;
					break;
				case 3:
					mask=0xffffffff;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			res&=mask;
			if(instr->has_nz) {
				(*op2reg(cpu, instr->op1))&=(~mask);
				(*op2reg(cpu, instr->op1))|=res;
			} else {
				(*op2reg(cpu, instr->op1))=res;
			}
			break;
		case XOR:
			if(instr->op1==0xe || instr->op1==0xf || instr->op2==0xe || instr->op2==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1))^(*op2reg(cpu, instr->op2));
			mask=0;
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					break;
				case 2:
					mask=0xffff;
					break;
				case 3:
					mask=0xffffffff;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			res&=mask;
			if(instr->has_nz) {
				(*op2reg(cpu, instr->op1))&=(~mask);
				(*op2reg(cpu, instr->op1))|=res;
			} else {
				(*op2reg(cpu, instr->op1))=res;
			}
			break;
		case NEG:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if(instr->has_nz) {
				// NZ: 高位保留，低 N 位按位取反
				uint32_t nv=*op2reg(cpu, instr->op1);
				switch(instr->op_size) {
					case 1: nv=(nv&0xffffff00)|(~nv&0xff); break;
					case 2: nv=(nv&0xffff0000)|(~nv&0xffff); break;
					default: nv=~nv; break;
				}
				*op2reg(cpu, instr->op1)=nv;
			} else {
				(*op2reg(cpu, instr->op1))=~(*op2reg(cpu, instr->op1));
			}
			break;
		case MNE:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if(instr->has_nz) {
				// NZ: 高位保留，低 N 位算术取反
				uint32_t nv=*op2reg(cpu, instr->op1);
				switch(instr->op_size) {
					case 1: nv=(nv&0xffffff00)|((uint32_t)(-(int32_t)(int8_t)(uint8_t)(nv&0xff))&0xff); break;
					case 2: nv=(nv&0xffff0000)|((uint32_t)(-(int32_t)(int16_t)(uint16_t)(nv&0xffff))&0xffff); break;
					default: nv=-nv; break;
				}
				*op2reg(cpu, instr->op1)=nv;
			} else {
				(*op2reg(cpu, instr->op1))=-(*op2reg(cpu, instr->op1));
			}
			break;
		case PUSH:
			if(instr->op1==0xe) {
				exe_err(cpu);
				err=1;
				return err;
			}
			// MPU: 压栈写区间 [S+1, S+size] 检查
			if(instr->op_size>=1 && instr->op_size<=3) {
				uint32_t s_new=*op2reg(cpu, REG_S)+(1u<<(instr->op_size-1));
				if(mem_check_data(cpu, *op2reg(cpu, REG_S)+1, 1u<<(instr->op_size-1))) {
					exe_err(cpu);
					cpu->sys.xar=mpu_stack_xar(cpu, s_new);
					return 3;		// #STACK
				}
			}
			if(instr->op1==0xf) {
				err=push(cpu, instr->imm, instr->op_size);
			} else {
				err=push(cpu, (*op2reg(cpu, instr->op1)), instr->op_size);
			}
			if(err) {
				exe_err(cpu);
				if(err==2) {		// 栈上溢 → #STACK
					cpu->sys.xar=((uint32_t)(*op2reg(cpu, REG_S))+instr->op_size)&0x7fffffff;
					return 3;
				}
				return 1;			// 尺寸非法 → #II
			}
			break;
		case POP:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if(instr->op_size==0) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if((*op2reg(cpu, REG_S))<(1u<<(instr->op_size-1))) {		// 栈下溢 → #STACK
				exe_err(cpu);
				cpu->sys.xar=0x80000000u|(((*op2reg(cpu, REG_S))-(1u<<(instr->op_size-1)))&0x7fffffff);
				return 3;
			}
			// MPU: 弹栈读区间 [S-size+1, S] 检查
			if(instr->op_size>=1 && instr->op_size<=3) {
				uint32_t s_new=*op2reg(cpu, REG_S)-(1u<<(instr->op_size-1));
				if(mem_check_data(cpu, s_new+1, 1u<<(instr->op_size-1))) {
					exe_err(cpu);
					cpu->sys.xar=mpu_stack_xar(cpu, s_new);
					return 3;		// #STACK
				}
			}
			res=pop(cpu, instr->op_size);
			// MPU: 弹栈到 E/S/F 时检查新值所在区间
			if(instr->op1==REG_E) {
				if(mem_check_code(cpu, res, 0)) { exe_err(cpu); return 4; }		// #GP
			} else if(instr->op1==REG_S || instr->op1==REG_F) {
				if(mem_check_data(cpu, res, 0)) { exe_err(cpu); cpu->sys.xar=mpu_stack_xar(cpu, res); return 3; }	// #STACK
			}
			if(instr->has_nz) {
				// NZ: 高位保留，低 N 位替换为弹栈值
				nval=(*op2reg(cpu, instr->op1));
				switch(instr->op_size) {
					case 1:
						nval=(nval&0xffffff00)|(res&0xff);
						break;
					case 2:
						nval=(nval&0xffff0000)|(res&0xffff);
						break;
					case 3:
						nval=res;
						break;
					default:
						break;
				}
				(*op2reg(cpu, instr->op1))=nval;
			} else {
				(*op2reg(cpu, instr->op1))=res;
			}
			break;
		case SFA:
			if(instr->op1!=0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if((uint64_t)(*op2reg(cpu, REG_S))+instr->imm>DATA_SIZE) {	// 显式修改S越界 → #STACK
				exe_err(cpu);
				cpu->sys.xar=((uint32_t)(*op2reg(cpu, REG_S))+instr->imm)&0x7fffffff;
				return 3;
			}
			// MPU: S 新值必须落在 [DBASE, DLIMIT)
			{
				uint32_t s_new=(*op2reg(cpu, REG_S))+instr->imm;
				if(mem_check_data(cpu, s_new, 0)) {
					exe_err(cpu);
					cpu->sys.xar=mpu_stack_xar(cpu, s_new);
					return 3;		// #STACK
				}
			}
			(*op2reg(cpu, REG_F))=(*op2reg(cpu, REG_S));
			(*op2reg(cpu, REG_S))+=instr->imm;
			break;
		case RER:
			{
				uint32_t fv=*op2reg(cpu, REG_F);
				if(fv>=DATA_SIZE) {		// S=F 越界（上溢）→ #STACK
					exe_err(cpu);
					cpu->sys.xar=fv&0x7fffffff;
					return 3;
				}
				if(fv<4) {				// 无法弹出E（下溢）→ #STACK
					exe_err(cpu);
					cpu->sys.xar=0x80000000u|((fv-4)&0x7fffffff);
					return 3;
				}
				// MPU: S=F 必须落在 [DBASE, DLIMIT)
				if(mem_check_data(cpu, fv, 0)) {
					exe_err(cpu);
					cpu->sys.xar=mpu_stack_xar(cpu, fv);
					return 3;			// #STACK
				}
				(*op2reg(cpu, REG_S))=fv;
				(*op2reg(cpu, REG_E))=pop(cpu, 3);
			}
			break;
		case PUSHR:
			// MPU: 压栈写区间 [S+1, S+4] 检查
			if(mem_check_data(cpu, *op2reg(cpu, REG_S)+1, 4)) {
				exe_err(cpu);
				cpu->sys.xar=mpu_stack_xar(cpu, (*op2reg(cpu, REG_S))+4);
				return 3;				// #STACK
			}
			if(push(cpu, (*op2reg(cpu, REG_R)), 3)) {
				exe_err(cpu);
				cpu->sys.xar=((uint32_t)(*op2reg(cpu, REG_S))+4)&0x7fffffff;
				return 3;
			}
			break;
		case POPR:
			if((*op2reg(cpu, REG_S))<4) {
				exe_err(cpu);
				cpu->sys.xar=0x80000000u|(((*op2reg(cpu, REG_S))-4)&0x7fffffff);
				return 3;
			}
			// MPU: 弹栈读区间 [S-3, S] 检查
			if(mem_check_data(cpu, (*op2reg(cpu, REG_S))-3, 4)) {
				exe_err(cpu);
				cpu->sys.xar=mpu_stack_xar(cpu, (*op2reg(cpu, REG_S))-4);
				return 3;				// #STACK
			}
			(*op2reg(cpu, REG_R))=pop(cpu, 3);
			break;
		case SRA:
			(*op2reg(cpu, REG_R))=(*op2reg(cpu, REG_A));
			break;
		case SRB:
			(*op2reg(cpu, REG_R))=(*op2reg(cpu, REG_B));
			break;
		case LOD:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			// MPU: *R 读取区间检查
			if(instr->op_size>=1 && instr->op_size<=3) {
				if(mem_check_data(cpu, *op2reg(cpu, REG_R), 1u<<(instr->op_size-1))) {
					exe_err(cpu);
					return 4;		// #GP
				}
			}
			switch(instr->op_size) {
				case 1:
					res=load_from_mem(cpu, (*op2reg(cpu, REG_R)), MEM_TYPE_DATA);
					(*op2reg(cpu, REG_R))++;
					break;
				case 2:
					res=load_from_mem(cpu, (*op2reg(cpu, REG_R)), MEM_TYPE_DATA);
					(*op2reg(cpu, REG_R))++;
					res|=(load_from_mem(cpu, (*op2reg(cpu, REG_R)), MEM_TYPE_DATA)<<8);
					(*op2reg(cpu, REG_R))++;
					break;
				case 3:
					res=load_from_mem(cpu, (*op2reg(cpu, REG_R)), MEM_TYPE_DATA);
					(*op2reg(cpu, REG_R))++;
					res|=(load_from_mem(cpu, (*op2reg(cpu, REG_R)), MEM_TYPE_DATA)<<8);
					(*op2reg(cpu, REG_R))++;
					res|=(load_from_mem(cpu, (*op2reg(cpu, REG_R)), MEM_TYPE_DATA)<<16);
					(*op2reg(cpu, REG_R))++;
					res|=(load_from_mem(cpu, (*op2reg(cpu, REG_R)), MEM_TYPE_DATA)<<24);
					(*op2reg(cpu, REG_R))++;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			(*op2reg(cpu, instr->op1))=res;
			break;
		case STO:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			// MPU: *R 写入区间检查
			if(instr->op_size>=1 && instr->op_size<=3) {
				if(mem_check_data(cpu, *op2reg(cpu, REG_R), 1u<<(instr->op_size-1))) {
					exe_err(cpu);
					return 4;		// #GP
				}
			}
			switch(instr->op_size) {
				case 1:
					set_mem(cpu, (*op2reg(cpu, REG_R)), ((*op2reg(cpu, instr->op1))&0xff), MEM_TYPE_DATA);
					(*op2reg(cpu, REG_R))++;
					break;
				case 2:
					set_word_mem(cpu, (*op2reg(cpu, REG_R)), (*op2reg(cpu, instr->op1)), MEM_TYPE_DATA);
					(*op2reg(cpu, REG_R))+=2;
					break;
				case 3:
					set_dword_mem(cpu, (*op2reg(cpu, REG_R)), (*op2reg(cpu, instr->op1)), MEM_TYPE_DATA);
					(*op2reg(cpu, REG_R))+=4;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			break;
		case SR:
			if(instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if(instr->op1==0xe) res=0;
			else res=(*op2reg(cpu, instr->op1));
			if(instr->op2==0xe);
			else {
				res+=(*op2reg(cpu, instr->op2))*(1<<(instr->sr_k));
			}
			res+=instr->imm;
			(*op2reg(cpu, REG_R))=res;
//			fprintf(stderr, "INFO: SR: instr->imm=%u, instr->sr_k=%u\n\n", instr->imm, instr->sr_k);
			break;
		case TEST:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			switch(instr->op_size) {
				case 1:
					(*op2reg(cpu, REG_C))&=((*op2reg(cpu, instr->op1))&0xff);
					break;
				case 2:
					(*op2reg(cpu, REG_C))&=((*op2reg(cpu, instr->op1))&0xffff);
					break;
				case 3:
					(*op2reg(cpu, REG_C))&=((*op2reg(cpu, instr->op1)));
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			break;
		case CMP:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			switch(instr->op_size) {
				case 1:
					(*op2reg(cpu, REG_C))-=((*op2reg(cpu, instr->op1))&0xff);
					break;
				case 2:
					(*op2reg(cpu, REG_C))-=((*op2reg(cpu, instr->op1))&0xffff);
					break;
				case 3:
					(*op2reg(cpu, REG_C))-=((*op2reg(cpu, instr->op1)));
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			break;
		case JMP:
			{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			break;
		case JZ:
			if(*op2reg(cpu, REG_C)==0) {
				{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			}
			break;
		case JNZ:
			if(*op2reg(cpu, REG_C)!=0) {
				{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			}
			break;
		case JRZ:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1));
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					break;
				case 2:
					mask=0xffff;
					break;
				case 3:
					mask=0xffffffff;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			if((res&mask)==0) {
				{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			}
			break;
		case JRNZ:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1));
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					break;
				case 2:
					mask=0xffff;
					break;
				case 3:
					mask=0xffffffff;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			if((res&mask)!=0) {
				{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			}
			break;
		case JA:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1));
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					break;
				case 2:
					mask=0xffff;
					break;
				case 3:
					mask=0xffffffff;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
//			fprintf(stderr, "res=%u, instr->op1=%d, C=%u\n", res&mask, instr->op1, (*op2reg(cpu, REG_C)&mask));
			if(((*op2reg(cpu, REG_C))&mask)>(res&mask)) {
				{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			}
			break;
		case JNA:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1));
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					break;
				case 2:
					mask=0xffff;
					break;
				case 3:
					mask=0xffffffff;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			if((*op2reg(cpu, REG_C)&mask)<=(res&mask)) {
				{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			}
			break;
		case JB:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1));
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					break;
				case 2:
					mask=0xffff;
					break;
				case 3:
					mask=0xffffffff;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			if((*op2reg(cpu, REG_C)&mask)<(res&mask)) {
				{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			}
			break;
		case JNB:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1));
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					break;
				case 2:
					mask=0xffff;
					break;
				case 3:
					mask=0xffffffff;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			if((*op2reg(cpu, REG_C)&mask)>=(res&mask)) {
				{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			}
			break;
		case JG:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1));
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					res=(int32_t)((int8_t)(res&mask));
					break;
				case 2:
					mask=0xffff;
					res=(int32_t)((int16_t)(res&mask));
					break;
				case 3:
					mask=0xffffffff;
					res=(int32_t)((int32_t)(res&mask));
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
//			fprintf(stderr, "res=%u, instr->op1=%d, C=%u\n", res&mask, instr->op1, (*op2reg(cpu, REG_C)&mask));
			// 有符号比较：C 与 reg 先按 mask 截断，再以 int32 解释（JG: C > reg）
			// （注意：不能写成 (int32_t)(C)&mask，位与会重新提升为无符号）
			if(((int32_t)((*op2reg(cpu, REG_C))&mask))>((int32_t)res)) {
				{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			}
			break;
		case JNG:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1));
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					res=(int32_t)((int8_t)(res&mask));
					break;
				case 2:
					mask=0xffff;
					res=(int32_t)((int16_t)(res&mask));
					break;
				case 3:
					mask=0xffffffff;
					res=(int32_t)((int32_t)(res&mask));
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			// 有符号比较：JNG: C <= reg
			if(((int32_t)((*op2reg(cpu, REG_C))&mask))<=((int32_t)res)) {
				{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			}
			break;
		case JL:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1));
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					res=(int32_t)((int8_t)(res&mask));
					break;
				case 2:
					mask=0xffff;
					res=(int32_t)((int16_t)(res&mask));
					break;
				case 3:
					mask=0xffffffff;
					res=(int32_t)((int32_t)(res&mask));
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			// 有符号比较：JL: C < reg
			if(((int32_t)((*op2reg(cpu, REG_C))&mask))<((int32_t)res)) {
				{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			}
			break;
		case JNL:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			res=(*op2reg(cpu, instr->op1));
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					res=(int32_t)((int8_t)(res&mask));
					break;
				case 2:
					mask=0xffff;
					res=(int32_t)((int16_t)(res&mask));
					break;
				case 3:
					mask=0xffffffff;
					res=(int32_t)((int32_t)(res&mask));
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			// 有符号比较：JNL: C >= reg
			if(((int32_t)((*op2reg(cpu, REG_C))&mask))>=((int32_t)res)) {
				{ int jr=jmp(cpu); if(jr){ exe_err(cpu); return jr; } }
			}
			break;
		case IN:
			if(instr->op1==0xe || instr->op1==0xf || instr->op2==0xe || instr->op_size==0) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if(instr->op2==0xf) nval=instr->imm;
			else nval=(*op2reg(cpu, instr->op2));
			res=device_read(cpu, nval, instr->op_size);
			if(cpu->dev_mgr.last_dev_not_found)	{		// 如果设备不存在就触发#II 
				exe_err(cpu);
				err=1;
				return err;
			}
			switch(instr->op_size) {
				case 1:
					mask=0xffffff00;
					break;
				case 2:
					mask=0xffff0000;
					break;
				case 3:
					mask=0x00000000;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			if(instr->has_nz) {
				res&=(~mask);
				res|=((*op2reg(cpu, instr->op1))&mask);
			} else {
				res&=(~mask);
			}
			(*op2reg(cpu, instr->op1))=res;
			break;
		case OUT:
			if(instr->op1==0xe || instr->op2==0xe) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if(instr->op1==0xf) nval=instr->imm;
			else nval=(*op2reg(cpu, instr->op1));
			if(instr->op2==0xf) res=instr->imm;
			else res=(*op2reg(cpu, instr->op2));
			switch(instr->op_size) {
				case 1:
					mask=0xff;
					break;
				case 2:
					mask=0xffff;
					break;
				case 3:
					mask=0xffffffff;
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			res&=mask;
			device_write(cpu, (uint16_t)nval, res, instr->op_size);
			if(cpu->dev_mgr.last_dev_not_found) {
				exe_err(cpu);
				err=1;
				return err;
			}
			break;
		case INT:
			if(instr->op1==0xe) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if(instr->op1==0xf) nval=instr->imm;
			else nval=(*op2reg(cpu, instr->op1));
			res=handle_intr(cpu, (uint8_t)(nval&0xff), true);
			if(res) {
				// 软件中断被拒（GIE关/嵌套不允许/越权）→ #GP；
				// 派发栈上溢 → 系统失败
				exe_err(cpu);
				return ((int)res==-3)?(-3):(4);
			}
			break;
		case PUSH_RIN1:
			if(mem_check_data(cpu, *op2reg(cpu, REG_S)+1, 4)) {
				exe_err(cpu);
				cpu->sys.xar=mpu_stack_xar(cpu, (*op2reg(cpu, REG_S))+4);
				return 3;
			}
			if(push(cpu, cpu->intr.rin1, 3)) {
				exe_err(cpu);
				cpu->sys.xar=((uint32_t)(*op2reg(cpu, REG_S))+4)&0x7fffffff;
				return 3;
			}
			break;
		case PUSH_RIN2:
			if(mem_check_data(cpu, *op2reg(cpu, REG_S)+1, 4)) {
				exe_err(cpu);
				cpu->sys.xar=mpu_stack_xar(cpu, (*op2reg(cpu, REG_S))+4);
				return 3;
			}
			if(push(cpu, cpu->intr.rin2, 3)) {
				exe_err(cpu);
				cpu->sys.xar=((uint32_t)(*op2reg(cpu, REG_S))+4)&0x7fffffff;
				return 3;
			}
			break;
		case POP_RIN1:
			if((*op2reg(cpu, REG_S))<4) {
				exe_err(cpu);
				cpu->sys.xar=0x80000000u|(((*op2reg(cpu, REG_S))-4)&0x7fffffff);
				return 3;
			}
			if(mem_check_data(cpu, (*op2reg(cpu, REG_S))-3, 4)) {
				exe_err(cpu);
				cpu->sys.xar=mpu_stack_xar(cpu, (*op2reg(cpu, REG_S))-4);
				return 3;
			}
			cpu->intr.rin1=pop(cpu, 3);
			break;
		case POP_RIN2:
			if((*op2reg(cpu, REG_S))<4) {
				exe_err(cpu);
				cpu->sys.xar=0x80000000u|(((*op2reg(cpu, REG_S))-4)&0x7fffffff);
				return 3;
			}
			if(mem_check_data(cpu, (*op2reg(cpu, REG_S))-3, 4)) {
				exe_err(cpu);
				cpu->sys.xar=mpu_stack_xar(cpu, (*op2reg(cpu, REG_S))-4);
				return 3;
			}
			cpu->intr.rin2=pop(cpu, 3);
			break;
		case PUSHI:
			if((uint64_t)(*op2reg(cpu, REG_S))+8>DATA_SIZE) {
				exe_err(cpu);
				cpu->sys.xar=((uint32_t)(*op2reg(cpu, REG_S))+8)&0x7fffffff;
				return 3;
			}
			if(mem_check_data(cpu, *op2reg(cpu, REG_S)+1, 8)) {
				exe_err(cpu);
				cpu->sys.xar=mpu_stack_xar(cpu, (*op2reg(cpu, REG_S))+8);
				return 3;
			}
			pushi(cpu);
			break;
		case POPI:
			if((*op2reg(cpu, REG_S))<8) {
				exe_err(cpu);
				cpu->sys.xar=0x80000000u|(((*op2reg(cpu, REG_S))-8)&0x7fffffff);
				return 3;
			}
			if(mem_check_data(cpu, (*op2reg(cpu, REG_S))-7, 8)) {
				exe_err(cpu);
				cpu->sys.xar=mpu_stack_xar(cpu, (*op2reg(cpu, REG_S))-8);
				return 3;
			}
			popi(cpu);
			break;
		case HLT:
			cpu->halted=1;
			break;
		case IRET:
			iret(cpu);
			break;
		case SVC:
			if(svc(cpu)!=0) {		// 派发失败（如栈上溢）→ 系统失败
				exe_err(cpu);
				return -3;
			}
			break;
		case SETB:
			if(cpu->intr.cpl!=0) {	// 用户态执行特权指令 → #GP
				exe_err(cpu);
				return 4;
			}
			if(instr->op2==0xe || instr->op2==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			// 注意：SYSREG编码位于op1半字节（见manual 0x40-0x43节）
			switch(instr->op1) {
				case 0: cpu->sys.cbase=(*op2reg(cpu, instr->op2)); break;
				case 1: cpu->sys.climit=(*op2reg(cpu, instr->op2)); break;
				case 2: cpu->sys.dbase=(*op2reg(cpu, instr->op2)); break;
				case 3: cpu->sys.dlimit=(*op2reg(cpu, instr->op2)); break;
				case 4: cpu->sys.ksp=(*op2reg(cpu, instr->op2)); break;
				case 5: cpu->intr.ctrl=(*op2reg(cpu, instr->op2));
						update_intr_context(&(cpu->intr));
						break;
				case 6: cpu->sys.xar=(*op2reg(cpu, instr->op2)); break;
				case 7: cpu->intr.ictb=(*op2reg(cpu, instr->op2)); break;
				case 8: cpu->fpcr=(*op2reg(cpu, instr->op2)); break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			break;
		case GETB:
			if(cpu->intr.cpl!=0) {	// 用户态执行特权指令 → #GP
				exe_err(cpu);
				return 4;
			}
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			switch(instr->op2) {
				case 0: (*op2reg(cpu, instr->op1))=cpu->sys.cbase; break;
				case 1: (*op2reg(cpu, instr->op1))=cpu->sys.climit; break;
				case 2: (*op2reg(cpu, instr->op1))=cpu->sys.dbase; break;
				case 3: (*op2reg(cpu, instr->op1))=cpu->sys.dlimit; break;
				case 4: (*op2reg(cpu, instr->op1))=cpu->sys.ksp; break;
				case 5: (*op2reg(cpu, instr->op1))=cpu->intr.ctrl; break;
				case 6: (*op2reg(cpu, instr->op1))=cpu->sys.xar; break;
				case 7: (*op2reg(cpu, instr->op1))=cpu->intr.ictb; break;
				case 8: (*op2reg(cpu, instr->op1))=cpu->fpcr; break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			break;
		case BLKS:
			if(instr->op1==0xe) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if(instr->op1==0xf) {
				res=instr->imm;
			} else {
				res=(*op2reg(cpu, instr->op1));
			}
			uint32_t blk_step=1<<(instr->op_size-1);	// BYTE=1, WORD=2, DWORD=4
			// MPU: 批量赋值区间 [R, R+C*step) 检查
			{
				uint32_t cnt=*op2reg(cpu, REG_C);
				if(cnt>0) {
					uint64_t end=(uint64_t)(*op2reg(cpu, REG_R))+(uint64_t)(cnt-1)*blk_step+(uint64_t)blk_step;
					if(mem_check_data(cpu, *op2reg(cpu, REG_R), (uint32_t)(end-(*op2reg(cpu, REG_R))))) {
						exe_err(cpu);
						return 4;		// #GP
					}
				}
			}
			for(uint32_t i=0; i<(*op2reg(cpu, REG_C)); i++) {
				switch(instr->op_size) {
					case 1:
						set_mem(cpu, (*op2reg(cpu, REG_R))+i*blk_step, res, MEM_TYPE_DATA);
						break;
					case 2:
						set_word_mem(cpu, (*op2reg(cpu, REG_R))+i*blk_step, res, MEM_TYPE_DATA);
						break;
					case 3:
						set_dword_mem(cpu, (*op2reg(cpu, REG_R))+i*blk_step, res, MEM_TYPE_DATA);
						break;
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
			}
			break;
		case PUSH_P:
			// MPU: 压栈写区间检查
			if(instr->op_size>=1 && instr->op_size<=3) {
				uint32_t s_new=*op2reg(cpu, REG_S)+(1u<<(instr->op_size-1));
				if(mem_check_data(cpu, *op2reg(cpu, REG_S)+1, 1u<<(instr->op_size-1))) {
					exe_err(cpu);
					cpu->sys.xar=mpu_stack_xar(cpu, s_new);
					return 3;		// #STACK
				}
			}
			{
				int perr=push(cpu, cpu->P, instr->op_size);
				if(perr) {
					exe_err(cpu);
					if(perr==2) {
						cpu->sys.xar=((uint32_t)(*op2reg(cpu, REG_S))+(1u<<(instr->op_size-1)))&0x7fffffff;
						return 3;	// #STACK
					}
					return 1;		// #II
				}
			}
			break;
		case NOP:
			break;
		case INC:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			(*op2reg(cpu, instr->op1))++;
			break;
		case DEC:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			(*op2reg(cpu, instr->op1))--;
			break;
		case BLKIN:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			uint32_t blkin_step=1<<(instr->op_size-1);	// BYTE=1, WORD=2, DWORD=4
			// MPU: 批量写入区间 [op1, op1+C*step) 检查（*E → 代码区间）
			{
				uint32_t cnt=*op2reg(cpu, REG_C);
				if(cnt>0) {
					uint64_t end=(uint64_t)(*op2reg(cpu, instr->op1))+(uint64_t)(cnt-1)*blkin_step+(uint64_t)blkin_step;
					int mc=(instr->op1==REG_E)
						? mem_check_code(cpu, *op2reg(cpu, instr->op1), (uint32_t)(end-(*op2reg(cpu, instr->op1))))
						: mem_check_data(cpu, *op2reg(cpu, instr->op1), (uint32_t)(end-(*op2reg(cpu, instr->op1))));
					if(mc) {
						exe_err(cpu);
						return 4;		// #GP
					}
				}
			}
			for(uint32_t i=0; i<(*op2reg(cpu, REG_C)); i++) {
				res=device_read(cpu, (uint16_t)((*op2reg(cpu, REG_A))&0xffff), instr->op_size);
				switch(instr->op_size) {
					case 1:
						set_mem(cpu, (*op2reg(cpu, instr->op1))+i*blkin_step, res, (instr->op1==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
						break;
					case 2:
						set_word_mem(cpu, (*op2reg(cpu, instr->op1))+i*blkin_step, res, (instr->op1==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
						break;
					case 3:
						set_dword_mem(cpu, (*op2reg(cpu, instr->op1))+i*blkin_step, res, (instr->op1==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
						break;
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
			}
			break;
		case POR:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if((*op2reg(cpu, REG_S))<(1u<<(instr->op_size-1))) {		// 栈下溢 → #STACK
				exe_err(cpu);
				cpu->sys.xar=0x80000000u|(((*op2reg(cpu, REG_S))-(1u<<(instr->op_size-1)))&0x7fffffff);
				return 3;
			}
			// MPU: 弹栈读区间 [S-size+1, S] 检查
			if(instr->op_size>=1 && instr->op_size<=3) {
				uint32_t s_new=*op2reg(cpu, REG_S)-(1u<<(instr->op_size-1));
				if(mem_check_data(cpu, s_new+1, 1u<<(instr->op_size-1))) {
					exe_err(cpu);
					cpu->sys.xar=mpu_stack_xar(cpu, s_new);
					return 3;		// #STACK
				}
			}
			res=pop(cpu, instr->op_size);
			uint32_t por_addr=(*op2reg(cpu, instr->op1));
			uint8_t por_type=(instr->op1==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA);
			// MPU: 写入目标区间检查（*DPR → 数据区间；*E → 代码区间）
			if(instr->op_size>=1 && instr->op_size<=3) {
				int mc=(instr->op1==REG_E)
					? mem_check_code(cpu, por_addr, 1u<<(instr->op_size-1))
					: mem_check_data(cpu, por_addr, 1u<<(instr->op_size-1));
				if(mc) {
					exe_err(cpu);
					return 4;		// #GP
				}
			}
			switch(instr->op_size) {
				case 1:
					set_mem(cpu, por_addr, (res&0xff), por_type);
					break;
				case 2:
					set_word_mem(cpu, por_addr, res, por_type);
					break;
				case 3:
					set_dword_mem(cpu, por_addr, res, por_type);
					break;
				default:
					exe_err(cpu);
					err=1;
					return err;
			}
			break;
			case FMOV:
				if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->fp_regs[instr->op1]=cpu->fp_regs[instr->op2];
break;
case FLDI:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
cpu->fp_regs[instr->op1]=instr->imm;
break;
case FLD: {
if(instr->op1>7 || instr->op2==0xe || instr->op2==0xf) { exe_err(cpu); err=1; return err; }
uint32_t fp_addr=*op2reg(cpu, instr->op2);
if(instr->op2==REG_E) {
if(mem_check_code(cpu, fp_addr, 4)) { exe_err(cpu); return 4; }
cpu->fp_regs[instr->op1]=load_dword_from_mem(cpu, fp_addr, MEM_TYPE_CODE);
} else {
if(mem_check_data(cpu, fp_addr, 4)) { exe_err(cpu); return 4; }
cpu->fp_regs[instr->op1]=load_dword_from_mem(cpu, fp_addr, MEM_TYPE_DATA);
}
break;
}
case FST: {
if(instr->op2>7 || instr->op1==0xe || instr->op1==0xf) { exe_err(cpu); err=1; return err; }
uint32_t fp_addr=*op2reg(cpu, instr->op1);
if(instr->op1==REG_E) {
if(mem_check_code(cpu, fp_addr, 4)) { exe_err(cpu); return 4; }
set_dword_mem(cpu, fp_addr, cpu->fp_regs[instr->op2], MEM_TYPE_CODE);
} else {
if(mem_check_data(cpu, fp_addr, 4)) { exe_err(cpu); return 4; }
set_dword_mem(cpu, fp_addr, cpu->fp_regs[instr->op2], MEM_TYPE_DATA);
}
break;
}
case FADD:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->fp_regs[instr->op1]=fp_float_to_bits(
fp_bits_to_float(cpu->fp_regs[instr->op1]) +
fp_bits_to_float(cpu->fp_regs[instr->op2]));
break;
case FSUB:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->fp_regs[instr->op1]=fp_float_to_bits(
fp_bits_to_float(cpu->fp_regs[instr->op1]) -
fp_bits_to_float(cpu->fp_regs[instr->op2]));
break;
case FMUL:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->fp_regs[instr->op1]=fp_float_to_bits(
fp_bits_to_float(cpu->fp_regs[instr->op1]) *
fp_bits_to_float(cpu->fp_regs[instr->op2]));
break;
case FDIV: {
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
float fp_a=fp_bits_to_float(cpu->fp_regs[instr->op1]);
float fp_b=fp_bits_to_float(cpu->fp_regs[instr->op2]);
if(fp_b==0.0f) cpu->fpcr |= (1u<<3);// DZ
cpu->fp_regs[instr->op1]=fp_float_to_bits(fp_a / fp_b);
break;
}
case FSQRT:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
{
float fp_a=fp_bits_to_float(cpu->fp_regs[instr->op1]);
if(fp_a<0.0f) cpu->fpcr |= (1u<<4);// INV
cpu->fp_regs[instr->op1]=fp_float_to_bits(sqrtf(fp_a));
}
break;
case FNEG:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
cpu->fp_regs[instr->op1] ^= 0x80000000u;
break;
case FABS:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
cpu->fp_regs[instr->op1] &= 0x7fffffffu;
break;
case FCMP: {
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
float fp_a=fp_bits_to_float(cpu->fp_regs[instr->op1]);
float fp_b=fp_bits_to_float(cpu->fp_regs[instr->op2]);
if(isnan(fp_a) || isnan(fp_b)) {
cpu->fpcr |= (1u<<4);// INV
cpu->regs[REG_C]=0;
} else if(fp_a < fp_b) {
cpu->regs[REG_C]=(uint32_t)-1;
} else if(fp_a > fp_b) {
cpu->regs[REG_C]=1;
} else {
cpu->regs[REG_C]=0;
}
break;
}
case F2I:
if(instr->op1==0xe || instr->op1==0xf || instr->op2>7) { exe_err(cpu); err=1; return err; }
{
float fp_a=fp_bits_to_float(cpu->fp_regs[instr->op2]);
*op2reg(cpu, instr->op1)=(uint32_t)(int32_t)fp_a;
}
break;
case I2F:
if(instr->op1>7 || instr->op2==0xe || instr->op2==0xf) { exe_err(cpu); err=1; return err; }
{
int32_t fp_i=(int32_t)(*op2reg(cpu, instr->op2));
cpu->fp_regs[instr->op1]=fp_float_to_bits((float)fp_i);
}
break;
case FPUSH:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
{
int perr=push(cpu, cpu->fp_regs[instr->op1], 3);
if(perr) {
exe_err(cpu);
if(perr==2) {
cpu->sys.xar=((uint32_t)(*op2reg(cpu, REG_S))+4)&0x7fffffff;
return 3;// #STACK
}
return 1;// #II
}
}
break;
case FPOP:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
if((*op2reg(cpu, REG_S))<4) {
exe_err(cpu);
cpu->sys.xar=0x80000000u|(((*op2reg(cpu, REG_S))-4)&0x7fffffff);
return 3;// #STACK
}
if(mem_check_data(cpu, *op2reg(cpu, REG_S)-3, 4)) {
exe_err(cpu);
cpu->sys.xar=mpu_stack_xar(cpu, *op2reg(cpu, REG_S)-4);
return 3;// #STACK
}
cpu->fp_regs[instr->op1]=pop(cpu, 3);
break;
case DMOV:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->dbl_regs[instr->op1]=cpu->dbl_regs[instr->op2];
break;
case DLDI:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
{
uint64_t bits=((uint64_t)instr->imm_hi<<32)|instr->imm;
double d; memcpy(&d, &bits, sizeof(d));
cpu->dbl_regs[instr->op1]=d;
}
break;
case DLD: {
if(instr->op1>7 || instr->op2==0xe || instr->op2==0xf) { exe_err(cpu); err=1; return err; }
uint32_t daddr=*op2reg(cpu, instr->op2);
if(instr->op2==REG_E) {
if(mem_check_code(cpu, daddr, 8)) { exe_err(cpu); return 4; }
cpu->dbl_regs[instr->op1]=dbl_from_mem(cpu, daddr, MEM_TYPE_CODE);
} else {
if(mem_check_data(cpu, daddr, 8)) { exe_err(cpu); return 4; }
cpu->dbl_regs[instr->op1]=dbl_from_mem(cpu, daddr, MEM_TYPE_DATA);
}
break;
}
case DST: {
if(instr->op2>7 || instr->op1==0xe || instr->op1==0xf) { exe_err(cpu); err=1; return err; }
uint32_t daddr=*op2reg(cpu, instr->op1);
if(instr->op1==REG_E) {
if(mem_check_code(cpu, daddr, 8)) { exe_err(cpu); return 4; }
dbl_to_mem(cpu, daddr, cpu->dbl_regs[instr->op2], MEM_TYPE_CODE);
} else {
if(mem_check_data(cpu, daddr, 8)) { exe_err(cpu); return 4; }
dbl_to_mem(cpu, daddr, cpu->dbl_regs[instr->op2], MEM_TYPE_DATA);
}
break;
}
case DADD:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->dbl_regs[instr->op1] += cpu->dbl_regs[instr->op2];
break;
case DSUB:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->dbl_regs[instr->op1] -= cpu->dbl_regs[instr->op2];
break;
case DMUL:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->dbl_regs[instr->op1] *= cpu->dbl_regs[instr->op2];
break;
case DDIV:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
if(cpu->dbl_regs[instr->op2]==0.0) cpu->fpcr |= (1u<<3);
cpu->dbl_regs[instr->op1] /= cpu->dbl_regs[instr->op2];
break;
case DSQRT:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
if(cpu->dbl_regs[instr->op1]<0.0) cpu->fpcr |= (1u<<4);
cpu->dbl_regs[instr->op1]=sqrt(cpu->dbl_regs[instr->op1]);
break;
case DNEG:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
cpu->dbl_regs[instr->op1] = -cpu->dbl_regs[instr->op1];
break;
case DABS:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
cpu->dbl_regs[instr->op1] = fabs(cpu->dbl_regs[instr->op1]);
break;
case DCMP: {
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
double da=cpu->dbl_regs[instr->op1], db=cpu->dbl_regs[instr->op2];
if(isnan(da) || isnan(db)) { cpu->fpcr |= (1u<<4); cpu->regs[REG_C]=0; }
else if(da<db) cpu->regs[REG_C]=(uint32_t)-1;
else if(da>db) cpu->regs[REG_C]=1;
else cpu->regs[REG_C]=0;
break;
}
case D2I:
if(instr->op1==0xe || instr->op1==0xf || instr->op2>7) { exe_err(cpu); err=1; return err; }
*op2reg(cpu, instr->op1)=(uint32_t)(int32_t)cpu->dbl_regs[instr->op2];
break;
case I2D:
if(instr->op1>7 || instr->op2==0xe || instr->op2==0xf) { exe_err(cpu); err=1; return err; }
cpu->dbl_regs[instr->op1]=(double)(int32_t)(*op2reg(cpu, instr->op2));
break;
case DPUSH:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
{
uint64_t bits; memcpy(&bits, &cpu->dbl_regs[instr->op1], sizeof(bits));
int perr=push(cpu, (uint32_t)(bits & 0xffffffffu), 3);
if(!perr) perr=push(cpu, (uint32_t)(bits>>32), 3);
if(perr) { exe_err(cpu); if(perr==2){ cpu->sys.xar=((uint32_t)(*op2reg(cpu, REG_S))+8)&0x7fffffff; return 3; } return 1; }
}
break;
case DPOP:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
if((*op2reg(cpu, REG_S))<8) { exe_err(cpu); cpu->sys.xar=0x80000000u|(((*op2reg(cpu, REG_S))-8)&0x7fffffff); return 3; }
{
uint32_t hi=pop(cpu, 3), lo=pop(cpu, 3);
uint64_t bits=((uint64_t)hi<<32)|lo;
memcpy(&cpu->dbl_regs[instr->op1], &bits, sizeof(bits));
}
break;

case F2D:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->dbl_regs[instr->op1]=(double)fp_bits_to_float(cpu->fp_regs[instr->op2]);
break;
case D2F:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->fp_regs[instr->op1]=fp_float_to_bits((float)cpu->dbl_regs[instr->op2]);
break;
case EMOV:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->ext_regs[instr->op1]=cpu->ext_regs[instr->op2];
break;
case ELDI:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
{
uint8_t b[10];
for(int i=0; i<4; i++) b[i]=(uint8_t)((instr->imm>>(8*i))&0xff);
for(int i=0; i<4; i++) b[i+4]=(uint8_t)((instr->imm_hi>>(8*i))&0xff);
b[8]=(uint8_t)(instr->imm_hi2&0xff);
b[9]=(uint8_t)((instr->imm_hi2>>8)&0xff);
cpu->ext_regs[instr->op1]=ext_from_bits(b);
}
break;
case ELD: {
if(instr->op1>7 || instr->op2==0xe || instr->op2==0xf) { exe_err(cpu); err=1; return err; }
uint32_t eaddr=*op2reg(cpu, instr->op2);
if(instr->op2==REG_E) {
if(mem_check_code(cpu, eaddr, 10)) { exe_err(cpu); return 4; }
cpu->ext_regs[instr->op1]=ext_from_mem(cpu, eaddr, MEM_TYPE_CODE);
} else {
if(mem_check_data(cpu, eaddr, 10)) { exe_err(cpu); return 4; }
cpu->ext_regs[instr->op1]=ext_from_mem(cpu, eaddr, MEM_TYPE_DATA);
}
break;
}
case EST: {
if(instr->op2>7 || instr->op1==0xe || instr->op1==0xf) { exe_err(cpu); err=1; return err; }
uint32_t eaddr=*op2reg(cpu, instr->op1);
if(instr->op1==REG_E) {
if(mem_check_code(cpu, eaddr, 10)) { exe_err(cpu); return 4; }
ext_to_mem(cpu, eaddr, cpu->ext_regs[instr->op2], MEM_TYPE_CODE);
} else {
if(mem_check_data(cpu, eaddr, 10)) { exe_err(cpu); return 4; }
ext_to_mem(cpu, eaddr, cpu->ext_regs[instr->op2], MEM_TYPE_DATA);
}
break;
}
case EADD:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->ext_regs[instr->op1] += cpu->ext_regs[instr->op2];
break;
case ESUB:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->ext_regs[instr->op1] -= cpu->ext_regs[instr->op2];
break;
case EMUL:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->ext_regs[instr->op1] *= cpu->ext_regs[instr->op2];
break;
case EDIV:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
if(cpu->ext_regs[instr->op2]==0.0L) cpu->fpcr |= (1u<<3);
cpu->ext_regs[instr->op1] /= cpu->ext_regs[instr->op2];
break;
case ESQRT:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
if(cpu->ext_regs[instr->op1]<0.0L) cpu->fpcr |= (1u<<4);
cpu->ext_regs[instr->op1]=sqrtl(cpu->ext_regs[instr->op1]);
break;
case ENEG:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
cpu->ext_regs[instr->op1] = -cpu->ext_regs[instr->op1];
break;
case EABS:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
cpu->ext_regs[instr->op1] = fabsl(cpu->ext_regs[instr->op1]);
break;
case ECMP: {
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
long double ea=cpu->ext_regs[instr->op1], eb=cpu->ext_regs[instr->op2];
if(isnan(ea) || isnan(eb)) { cpu->fpcr |= (1u<<4); cpu->regs[REG_C]=0; }
else if(ea<eb) cpu->regs[REG_C]=(uint32_t)-1;
else if(ea>eb) cpu->regs[REG_C]=1;
else cpu->regs[REG_C]=0;
break;
}
case E2I:
if(instr->op1==0xe || instr->op1==0xf || instr->op2>7) { exe_err(cpu); err=1; return err; }
*op2reg(cpu, instr->op1)=(uint32_t)(int32_t)cpu->ext_regs[instr->op2];
break;
case I2E:
if(instr->op1>7 || instr->op2==0xe || instr->op2==0xf) { exe_err(cpu); err=1; return err; }
cpu->ext_regs[instr->op1]=(long double)(int32_t)(*op2reg(cpu, instr->op2));
break;
case F2E:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->ext_regs[instr->op1]=(long double)fp_bits_to_float(cpu->fp_regs[instr->op2]);
break;
case E2F:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->fp_regs[instr->op1]=fp_float_to_bits((float)cpu->ext_regs[instr->op2]);
break;
case D2E:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->ext_regs[instr->op1]=(long double)cpu->dbl_regs[instr->op2];
break;
case E2D:
if(instr->op1>7 || instr->op2>7) { exe_err(cpu); err=1; return err; }
cpu->dbl_regs[instr->op1]=(double)cpu->ext_regs[instr->op2];
break;
case EPUSH:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
{
uint8_t b[10];
ext_to_bits(cpu->ext_regs[instr->op1], b);
uint32_t lo=((uint32_t)b[0])|(((uint32_t)b[1])<<8)|(((uint32_t)b[2])<<16)|(((uint32_t)b[3])<<24);
uint32_t mid=((uint32_t)b[4])|(((uint32_t)b[5])<<8)|(((uint32_t)b[6])<<16)|(((uint32_t)b[7])<<24);
uint16_t hi=(uint16_t)(((uint16_t)b[8])|(((uint16_t)b[9])<<8));
int perr=push(cpu, lo, 3);
if(!perr) perr=push(cpu, mid, 3);
if(!perr) perr=push(cpu, hi, 2);
if(perr) { exe_err(cpu); if(perr==2){ cpu->sys.xar=((uint32_t)(*op2reg(cpu, REG_S))+10)&0x7fffffff; return 3; } return 1; }
}
break;
case EPOP:
if(instr->op1>7) { exe_err(cpu); err=1; return err; }
if((*op2reg(cpu, REG_S))<10) { exe_err(cpu); cpu->sys.xar=0x80000000u|(((*op2reg(cpu, REG_S))-10)&0x7fffffff); return 3; }
{
uint16_t hi=(uint16_t)pop(cpu, 2);
uint32_t mid=pop(cpu, 3);
uint32_t lo=pop(cpu, 3);
uint8_t b[10];
for(int i=0; i<4; i++) b[i]=(uint8_t)((lo>>(8*i))&0xff);
for(int i=0; i<4; i++) b[i+4]=(uint8_t)((mid>>(8*i))&0xff);
b[8]=(uint8_t)(hi&0xff);
b[9]=(uint8_t)((hi>>8)&0xff);
cpu->ext_regs[instr->op1]=ext_from_bits(b);
}
break;
case TRA:
if(instr->op1==0xe || instr->op1==0xf) { exe_err(cpu); err=1; return err; }
int reg2=*op2reg(cpu, instr->op2);
int reg1=*op2reg(cpu, instr->op1);
// MPU: 数据指针解引用检查（*E 读代码空间，manual 未列入检查）
if(instr->op2!=REG_E && instr->op_size>=1 && instr->op_size<=3) {
	if(mem_check_data(cpu, (uint32_t)reg2, 1u<<(instr->op_size-1))) {
		exe_err(cpu);
		return 4;		// #GP
	}
}
if(instr->op1!=REG_E && instr->op_size>=1 && instr->op_size<=3) {
	if(mem_check_data(cpu, (uint32_t)reg1, 1u<<(instr->op_size-1))) {
		exe_err(cpu);
		return 4;		// #GP
	}
}
if(instr->op1==REG_E) {
	if(mem_check_code(cpu, (uint32_t)reg1, 1u<<(instr->op_size-1))) {
		exe_err(cpu);
		return 4;		// #GP
	}
}
if(instr->op2==REG_E) {
	if(mem_check_code(cpu, (uint32_t)reg2, 1u<<(instr->op_size-1))) {
		exe_err(cpu);
		return 4;		// #GP
	}
}
switch(instr->op_size) {
	case 1:
		res=load_from_mem(cpu, (uint32_t)reg2, (instr->op2==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
		break;
	case 2:
		res=load_word_from_mem(cpu, (uint32_t)reg2, (instr->op2==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
		break;
	case 3:
		res=load_dword_from_mem(cpu, (uint32_t)reg2, (instr->op2==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
		break;
	default:
		exe_err(cpu);
		err=1;
		return err;
}
switch(instr->op_size) {
	case 1:
		set_mem(cpu, (uint32_t)reg1, (res&0xff), (instr->op1==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
		break;
	case 2:
		set_word_mem(cpu, (uint32_t)reg1, (res&0xffff), (instr->op1==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
		break;
	case 3:
		set_dword_mem(cpu, (uint32_t)reg1, res, (instr->op1==REG_E)?(MEM_TYPE_CODE):(MEM_TYPE_DATA));
		break;
	default:
		exe_err(cpu);
		err=1;
}
break;

default:
			exe_err(cpu);
			err=1;
	}
	return err;
}

