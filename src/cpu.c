#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "decode.h"
#include "execution.h"

void cpu_init(DOCTOR_CPU *cpu) {
	cpu->code_mem=(uint8_t *)calloc(CODE_SIZE, 1);
	cpu->data_mem=(uint8_t *)calloc(DATA_SIZE, 1);

	if(!cpu->code_mem || !cpu->data_mem) {
		fprintf(stderr, "FATAL: 无法分配内存\n");
		exit(1);
	}

	memset(cpu->regs, 0, sizeof(cpu->regs));
	cpu->P=0;
	cpu->halted=false;

	memset(&cpu->sys, 0, sizeof(cpu->sys));

	cpu->sys.cbase=0;
	cpu->sys.climit=0xffffffff;
	cpu->sys.dbase=0;
	cpu->sys.dlimit=0xffffffff;

	cpu->intr.ctrl=0;
	cpu->intr.rin1=0;
	cpu->intr.rin2=0;
	cpu->intr.ictb=0;
	
	cpu->intr.current_int=-1;		// -1: 无中断
	cpu->intr.irq_base=0;
	cpu->intr.intr_enable=0;
	cpu->intr.inside_ppi=0;
	cpu->intr.inl=0;
	cpu->intr.exception=0;
	cpu->intr.cpl=0;

	return;
}

void cpu_load_bin(DOCTOR_CPU *cpu, const char *filename) {
	FILE *fp=fopen(filename, "rb");
	if(!fp) {
		fprintf(stderr, "FATAL: 无法打开文件: %s\n", filename);
		exit(1);
	}
	fseek(fp, 0, SEEK_END);
	size_t file_size=ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if(file_size>CODE_SIZE) {
		fprintf(stderr, "FATAL: 代码文件过大: %s\n", filename);
		fclose(fp);
		exit(1);
	}
	size_t read_len=fread(cpu->code_mem, 1, file_size, fp);
	fclose(fp);
	fprintf(stderr, "INFO: 成功加载文件: %s, 大小: %zu 字节\n", filename, read_len);
	return;
}

void cpu_run(DOCTOR_CPU *cpu) {
	fprintf(stderr, "INFO: CODE: %dMB, DATA: %dMB\n", CODE_SIZE/1024/1024, DATA_SIZE/1024/1024);
	uint64_t step_cnt=0, err_cnt=0;
	while((!cpu->halted) && (!sigint_received)) {
		if(cpu->P>=CODE_SIZE) {
			fprintf(stderr, "WARNING: P 越界: 0x%08X\n", cpu->P);
			cpu->P%=CODE_SIZE;
		}
		Decoded_instr instr;
		instr_init(&instr);
		int err=0;
		int de=decode(cpu, &instr);
		if(de==-1) {
			de=2;
//			cpu->halted=1;
		}
		cpu->P+=de;
		err=execute(cpu, &instr);
		if(err) err_cnt++;
		if(err==1) {
			fprintf(stderr, "INFO: ERR: #II\n");
		} 
		if(err==2) {
			fprintf(stderr, "INFO: ERR: #DIV0\n");
		}
		if(err_cnt==2) {
			fprintf(stderr, "FATAL: DOUBLE ERR\n");
			break;
		}
		step_cnt++;
	}
	fprintf(stderr, "INFO: step_cnt=%lu\n", step_cnt);
	exe_err(cpu);
	return;
}

void cpu_free(DOCTOR_CPU *cpu) {
	if(cpu->code_mem) free(cpu->code_mem);
	if(cpu->data_mem) free(cpu->data_mem);
}

