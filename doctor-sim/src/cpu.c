#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "decode.h"
#include "execution.h"
#include "interrupt.h"
#include "input.h"
#include <unistd.h>

#include "devices/pit.h"
#include "devices/fb.h"
#include "devices/uart.h"
#include "devices/kbc.h"
#include "devices/disk.h"
#include "devices/rtc.h"

Device dev_pit;
Device dev_fb;
Device dev_uart;
Device dev_kbc;
Device dev_disk;
Device dev_rtc;

void cpu_init(DOCTOR_CPU *cpu) {
	cpu->code_mem=(uint8_t *)calloc(CODE_SIZE, 1);
	cpu->data_mem=(uint8_t *)calloc(DATA_SIZE, 1);

	if(!cpu->code_mem || !cpu->data_mem) {
		fprintf(stderr, "FATAL: 无法分配内存\n");
		exit(1);
	}

	memset(cpu->regs, 0, sizeof(cpu->regs));
	memset(cpu->fp_regs, 0, sizeof(cpu->fp_regs));
	memset(cpu->dbl_regs, 0, sizeof(cpu->dbl_regs));
	memset(cpu->ext_regs, 0, sizeof(cpu->ext_regs));
	cpu->fpcr=0;
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

	pit_init(&dev_pit);
	device_register(cpu, &dev_pit);
	fb_init(&dev_fb);
	device_register(cpu, &dev_fb);
	uart_init(&dev_uart);
	device_register(cpu, &dev_uart);
	kbc_init(&dev_kbc);
	device_register(cpu, &dev_kbc);
	disk_init(&dev_disk);
	device_register(cpu, &dev_disk);
	rtc_init(&dev_rtc);
	device_register(cpu, &dev_rtc);

	return;
}

// 通用的镜像加载: 把文件内容读入内存区
static int load_file_to_mem(const char *filename, uint8_t *mem, size_t mem_size, const char *what) {
	FILE *fp=fopen(filename, "rb");
	if(!fp) return -1;						// 文件不存在/无法打开
	fseek(fp, 0, SEEK_END);
	long file_size=ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if(file_size<0 || (size_t)file_size>mem_size) {
		fprintf(stderr, "FATAL: %s文件过大: %s\n", what, filename);
		fclose(fp);
		return -1;
	}
	size_t read_len=fread(mem, 1, (size_t)file_size, fp);
	fclose(fp);
	fprintf(stderr, "INFO: 成功加载%s文件: %s, 大小: %zu 字节\n", what, filename, read_len);
	return 0;
}

int cpu_load_bin(DOCTOR_CPU *cpu, const char *filename) {
	return load_file_to_mem(filename, cpu->code_mem, CODE_SIZE, "代码");
}

int cpu_load_data_bin(DOCTOR_CPU *cpu, const char *filename) {
	return load_file_to_mem(filename, cpu->data_mem, DATA_SIZE, "数据");
}

void cpu_run(DOCTOR_CPU *cpu) {
	fprintf(stderr, "INFO: CODE: %dMB, DATA: %dMB\n", CODE_SIZE/1024/1024, DATA_SIZE/1024/1024);
	uint64_t step_cnt=0;
	int exc_streak=0;		// 连续异常计数（防止异常风暴/递归）
	while(!sigint_received) {
		// 宿主键盘输入 → KBC 设备（含安全键处理）
		input_poll(cpu);
		// 暂停：冻结模拟（Ctrl+C 暂停/恢复，见 input.c）
		// 若暂停且有剩余单步步数，则继续执行一步
		if(sim_paused && sim_steps_remaining==0) {
			usleep(1000);
			continue;
		}
		// 未停机：取指、解码、执行一条指令
		if(!cpu->halted) {
			if(cpu->P>=CODE_SIZE) {
				fprintf(stderr, "WARNING: P 越界: 0x%08X\n", cpu->P);
				cpu->P%=CODE_SIZE;
			}
			Decoded_instr instr;
			instr_init(&instr);
			uint32_t instr_addr=cpu->P;		// 指令起始地址（异常 XAR 用）
			int de=decode(cpu, &instr);
			if(de==-1) {
				// 非法指令 → #II。P 前进 2 字节（IRET 返回其后一条指令），不执行。
				de=2;
				cpu->P+=de;
				if(raise_exception(cpu, 0x01, instr_addr)!=0) {
					fprintf(stderr, "FATAL: 异常派发失败\n");
					break;
				}
				exc_streak++;
			} else {
				cpu->P+=de;
				int err=0;
				// REP 前缀：以 C 为计数器重复执行。
				// 语义：while (C != 0) { 执行指令; C--; }
				// （注意：不要与修改 C 的指令（CSI/CDI/TEST/CMP）组合，否则可能不终止）
				if(instr.has_rep) {
					while(cpu->regs[REG_C]!=0 && !cpu->halted) {
						err=execute(cpu, &instr);
						if(err) break;
						cpu->regs[REG_C]--;
					}
				} else {
					err=execute(cpu, &instr);
				}
				if(err) {
					if(err<0) {		// 系统级失败（派发栈上溢等）→ 停机
						fprintf(stderr, "FATAL: 指令执行失败 (err=%d)\n", err);
						break;
					}
					int hr=0;
					switch(err) {
						case 2: hr=raise_exception(cpu, 0x00, instr_addr); break;			// #DIV
						case 3: hr=raise_exception(cpu, 0x02, cpu->sys.xar); break;			// #STACK
						case 4: hr=raise_exception(cpu, 0x03, instr_addr); break;			// #GP
						default: hr=raise_exception(cpu, 0x01, instr_addr); break;			// #II
					}
					if(hr!=0) {
						fprintf(stderr, "FATAL: 异常派发失败 (err=%d, hr=%d)\n", err, hr);
						break;
					}
					exc_streak++;
				} else {
					exc_streak=0;
				}
			}
			if(exc_streak>6) {
				fprintf(stderr, "FATAL: 连续异常过多（异常风暴）\n");
				break;
			}
			step_cnt++;
		}
		// 指令边界（或HLT停机等待期间）：tick所有设备
		device_tick_all(cpu, 1);
		// 检查挂起的硬件中断，尝试派发
		int next_intr=get_next_intr(cpu);
		if(next_intr>=0) {
			int hr=handle_intr(cpu, (uint8_t)next_intr, false);
			if(hr==0) {
				cpu->halted=false;		// 中断成功派发（可唤醒HLT停机）
			} else if(hr==ERR_GP) {
				// 中断越权 → 触发 #GP 异常
				if(raise_exception(cpu, 0x03, cpu->P)!=0) {
					fprintf(stderr, "FATAL: 异常派发失败\n");
					break;
				}
			} else if(hr==-3) {
				fprintf(stderr, "FATAL: 中断派发栈上溢\n");
				break;
			} else if(hr==-4) {
				fprintf(stderr, "FATAL: ICT 表项越界（ICTB 无效）\n");
				break;
			}
			// hr==-1(中断关闭) / -2(嵌套拒绝)：保留挂起位，等待下次再试
		}

		// 暂停单步模式：每经过一次主循环递减一步
		if(sim_paused && sim_steps_remaining>0) {
			sim_steps_remaining--;
			if(sim_steps_remaining==0) {
				input_pause_prompt();
			}
		}
	}
	fprintf(stderr, "INFO: step_cnt=%lu\n", step_cnt);
	exe_err(cpu);
	return;
}

void cpu_free(DOCTOR_CPU *cpu) {
	if(cpu->code_mem) free(cpu->code_mem);
	if(cpu->data_mem) free(cpu->data_mem);
	// 释放设备私有数据（PIT/FB/UART 等）
	device_destroy_all(cpu);
}

