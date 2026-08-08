#include "execution.h"
#include "mem.h"
#include "debugger.h"
#include <stdio.h>

static inline void jmp(DOCTOR_CPU *cpu) {
	cpu->P=*op2reg(cpu, REG_E);
	return;
}

int execute(DOCTOR_CPU *cpu, Decoded_instr *instr) {
//	exe_err(cpu);
	int err=0;
	uint32_t res=0;
	switch(instr->opcode) {
		case LET:
//			fprintf(stderr, "INFO: P=0x%08X, LET %u %u, %u(0x%08X)\n", 
			if(instr->op2!=0xf) {
				exe_err(cpu);
				err=1;
			} else {
				if(instr->has_nz) {
					res=*op2reg(cpu, instr->op1);
					switch(instr->op_size) {
						case 1:
							res&=0xffffff00;
							res+=instr->imm;
							break;
						case 2:
							res&=0xffff0000;
							res+=instr->imm;
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
					res=*op2reg(cpu, instr->op1);
					switch(instr->op_size) {
						case 1:
							res&=0xffffff00;
							res+=((*op2reg(cpu, instr->op2))&0x000000ff);
							break;
						case 2:
							res&=0xffff0000;
							res+=((*op2reg(cpu, instr->op2))&0x0000ffff);
							break;
						case 3:
							res=instr->op2;
							break;
						default:
							exe_err(cpu);
							err=1;
							return err;
					}
				} else {
					res=*op2reg(cpu, instr->op2);
					switch(instr->op_size) {
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
			if(instr->has_rep) {
				if((*op2reg(cpu, REG_C))%2==0) {
					break;
				}
			}
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
				switch(instr->op_size) {
					case 1:
						uint8_t num8=(uint8_t)(nval&0xff);
						num8+=(uint8_t)(js&0xff);
						nval=num8;
						break;
					case 2:
						uint16_t num16=(uint16_t)(nval&0xffff);
						num16+=(uint16_t)(js&0xffff);
						nval=num16;
						break;
					case 3:
						nval=nval+js;
						break;
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
				*op2reg(cpu, instr->op1)=nval;
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
				switch(instr->op_size) {
					case 1:
						uint8_t num8=(uint8_t)(nvals&0xff);
						num8+=(uint8_t)(jss&0xff);
						nvals=num8;
						break;
					case 2:
						uint16_t num16=(uint16_t)(nvals&0xffff);
						num16+=(uint16_t)(jss&0xffff);
						nvals=num16;
						break;
					case 3:
						nvals=nvals+jss;
						break;
					default:
						exe_err(cpu);
						err=1;
						return err;
				}
				*op2reg(cpu, instr->op1)=nvals;
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
			fprintf(stderr, "INFO: dq_1=0x%lX, dq_2=0x%lX\n", dq_1, dq_2);
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
			instr->imm &= 0x1f;
			(*op2reg(cpu, instr->op1))<<=(instr->imm);
			break;
		case SHR:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			instr->imm &= 0x1f;
			(*op2reg(cpu, instr->op1))>>=(instr->imm);
			break;
		case MSR:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			uint8_t msr_sign=(*op2reg(cpu, instr->op1)>>31)&1;
			instr->imm &= 0x1f;
			uint32_t msr_res=(*op2reg(cpu, instr->op1));
			msr_res>>=instr->imm;
			if(msr_sign && instr->imm>0) msr_res|=(0xffffffff<<(32-instr->imm));
			(*op2reg(cpu, instr->op1))=msr_res;
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
			(*op2reg(cpu, instr->op1))=~(*op2reg(cpu, instr->op1));
			break;
		case MNE:
			if(instr->op1==0xe || instr->op1==0xf) {
				exe_err(cpu);
				err=1;
				return err;
			}
			(*op2reg(cpu, instr->op1))=-(*op2reg(cpu, instr->op1));
			break;
		case PUSH:
			if(instr->op1==0xe) {
				exe_err(cpu);
				err=1;
				return err;
			}
			if(instr->op1==0xf) {
				err=push(cpu, instr->imm, instr->op_size);
			} else {
				err=push(cpu, (*op2reg(cpu, instr->op1)), instr->op_size);
			}
			if(err) {
				exe_err(cpu);
				return err;
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
			res=pop(cpu, instr->op_size);
			if(instr->has_nz) {
				nval=(*op2reg(cpu, instr->op1));
				switch(instr->op_size) {
					case 1:
						nval&=0xff;
						nval+=res;
						break;
					case 2:
						nval&=0xffff;
						nval+=res;
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
			(*op2reg(cpu, REG_F))=(*op2reg(cpu, REG_S));
			(*op2reg(cpu, REG_S))+=instr->imm;
			break;
		case RER:
			(*op2reg(cpu, REG_S))=(*op2reg(cpu, REG_F));
			(*op2reg(cpu, REG_E))=pop(cpu, 3);
			break;
		case PUSHR:
			push(cpu, (*op2reg(cpu, REG_R)), 3);
			break;
		case POPR:
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
			jmp(cpu);
			break;
		case JZ:
			if(*op2reg(cpu, REG_C)==0) {
				jmp(cpu);
			}
			break;
		case JNZ:
			if(*op2reg(cpu, REG_C)!=0) {
				jmp(cpu);
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
				jmp(cpu);
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
				jmp(cpu);
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
				jmp(cpu);
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
				jmp(cpu);
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
				jmp(cpu);
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
				jmp(cpu);
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
			if(((int32_t)(*op2reg(cpu, REG_C))&mask)>(res&mask)) {
				jmp(cpu);
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
			if(((int32_t)(*op2reg(cpu, REG_C))&mask)<=(res&mask)) {
				jmp(cpu);
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
			if(((int32_t)(*op2reg(cpu, REG_C))&mask)<(res&mask)) {
				jmp(cpu);
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
			if(((int32_t)(*op2reg(cpu, REG_C))&mask)>=(res&mask)) {
				jmp(cpu);
			}
			break;
		case IN:
			//
		default:
			exe_err(cpu);
			err=1;
	}
	return err;
}

