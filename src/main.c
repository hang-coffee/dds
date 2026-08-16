#include <stdio.h>
#include "stdlib.h"
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>
#include <string.h>
#include <stdbool.h>
#include "debugger.h"
#include "cpu.h"
#include "decode.h"
#include "device.h"
#include "display.h"
#include "input.h"

#define PROGRAM_NAME      "doctor-emu"
#define DOCTOR_EMU_VERSION "0.1.0"

DOCTOR_CPU cpu;
static sigjmp_buf sigsegv_env;
static volatile sig_atomic_t segv_occ=0;

static void handle_sigsegv(int sig, siginfo_t *info, void *context) {
	sig=sig;
	info=info;
	context=context;
	signal(SIGSEGV, SIG_DFL);
	segv_occ=1;
	siglongjmp(sigsegv_env, 1);
}

static void init_segv_handler(void) {
	struct sigaction sa;
	sa.sa_sigaction=handle_sigsegv;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags=SA_SIGINFO;
	if(sigaction(SIGSEGV, &sa, NULL)==-1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
	return;
}

static void print_help(FILE *out) {
	fprintf(out,
		"用法: %s [选项] [-f [code <code.bin>] [data <data.bin>]]\n"
		"\n"
		"DOCTOR 架构模拟器 (Architecture v3.3)\n"
		"\n"
		"选项:\n"
		"  -f, --file [code <文件>] [data <文件>]   指定代码/数据镜像文件并启动。\n"
		"                                          指定了其中任意子参数时，未提及的那个文件\n"
		"                                          将不被加载（数据区保持全零）。\n"
		"                                          不带任何子参数时，等价于默认加载。\n"
		"  --display <tty|ansi|ppm|fb>             选择显示后端（默认 tty）。\n"
		"                                          tty=字符输出到终端; ansi=带颜色/字符画;\n"
		"                                          ppm=帧缓冲导出 PPM 图像; fb=帧缓冲留内存。\n"
		"  --display-file <文件>                    PPM 后端输出文件（默认 display.ppm）。\n"
		"  --display-size <宽x高>                   帧缓冲尺寸（默认 320x200）。\n"
		"  --dump-devices                            启动时打印设备状态（诊断用）。\n"
		"  -h, --help                               显示本帮助信息并退出。\n"
		"  -v, --version                            显示版本信息并退出。\n"
		"\n"
		"不带 -f 时，默认加载当前目录下的 code.bin 与 data.bin 并启动。\n"
		"若 data 文件不存在，仅给出警告，数据区保持全零。\n",
		PROGRAM_NAME);
	return;
}

static void print_version(void) {
	printf("%s %s (DOCTOR Architecture v3.3)\n", PROGRAM_NAME, DOCTOR_EMU_VERSION);
	printf("Copyright (C) 2026 Hangco. GNU GPL v3.\n");
	return;
}

int main(int argc, char *argv[]) {
	const char *code_file=NULL;
	const char *data_file=NULL;
	bool code_specified=false;		// 是否明确指定了code文件
	bool data_specified=false;		// 是否明确指定了data文件
	bool f_given=false;				// 是否出现了-f/--file

	// ---- Display 配置 ----
	Display_backend dsp_backend=DISPLAY_BACKEND_TTY;
	const char *dsp_file=NULL;
	uint32_t dsp_w=320, dsp_h=200;
	bool dump_devices=false;	// --dump-devices: 启动时打印设备状态

	// ---- 命令行解析 ----
	for(int i=1; i<argc; i++) {
		const char *arg=argv[i];

		if(strcmp(arg, "-h")==0 || strcmp(arg, "--help")==0) {
			print_help(stdout);
			return 0;
		}
		if(strcmp(arg, "-v")==0 || strcmp(arg, "--version")==0) {
			print_version();
			return 0;
		}
		if(strcmp(arg, "--display")==0) {
			if(i+1<argc) {
				dsp_backend=display_backend_from_name(argv[++i]);
			}
			continue;
		}
		if(strcmp(arg, "--display-file")==0) {
			if(i+1<argc) dsp_file=argv[++i];
			continue;
		}
		if(strcmp(arg, "--display-size")==0) {
			if(i+1<argc) {
				unsigned w=0, h=0;
				if(sscanf(argv[++i], "%ux%u", &w, &h)==2 && w>0 && h>0) {
					dsp_w=w; dsp_h=h;
				}
			}
			continue;
		}
		if(strcmp(arg, "--dump-devices")==0) {
			dump_devices=true;
			continue;
		}
		if(strcmp(arg, "-f")==0 || strcmp(arg, "--file")==0) {
			f_given=true;
			bool any=false;
			while(i+1<argc) {
				const char *next=argv[i+1];
				if(strcmp(next, "code")==0) {
					if(i+2>=argc) {
						fprintf(stderr, "FATAL: -f code 后缺少文件名\n");
						return 1;
					}
					code_file=argv[i+2];
					i+=2;
					code_specified=true;
					any=true;
				} else if(strcmp(next, "data")==0) {
					if(i+2>=argc) {
						fprintf(stderr, "FATAL: -f data 后缺少文件名\n");
						return 1;
					}
					data_file=argv[i+2];
					i+=2;
					data_specified=true;
					any=true;
				} else {
					break;		// 不是 code/data 子参数，结束 -f 解析
				}
			}
			if(!any) {
				// -f 无子参数：等价于默认加载
				code_file="code.bin";
				data_file="data.bin";
				code_specified=true;
				data_specified=true;
			}
			continue;
		}

		fprintf(stderr, "未知选项: %s\n", arg);
		print_help(stderr);
		return 1;
	}

	// 未出现 -f：默认加载当前目录的 code.bin 与 data.bin
	// （-f 有子参数时，未提及的那个文件保持未指定 → 不加载）
	if(!f_given) {
		if(!code_specified) {
			code_file="code.bin";
			code_specified=true;
		}
		if(!data_specified) {
			data_file="data.bin";
			data_specified=true;
		}
	}

	signal(SIGINT, handle_sigint);
	init_segv_handler();

	// ---- 初始化 Display 层（后端抽象；设备通过 display_get_global 使用）----
	if(dsp_file) display_set_ppm_path(dsp_file);
	display_init(display_get_global(), dsp_backend, dsp_w, dsp_h);
	display_puts(display_get_global(), "doctor-emu (DOCTOR v3.3) - display backend: ",
	             DISPLAY_FG_GREEN);
	display_puts(display_get_global(), display_backend_name(dsp_backend), DISPLAY_FG_CYAN|DISPLAY_ATTR_BOLD);
	display_puts(display_get_global(), "\n", 0);

	// ---- 宿主键盘输入 → KBC（安全键 Ctrl+C / q）----
	input_init();

	cpu_init(&cpu);
	if(dump_devices) device_dump_all(&cpu);
	if(code_specified) {
		if(cpu_load_bin(&cpu, code_file)!=0) {
			fprintf(stderr, "FATAL: 无法加载代码文件: %s\n", code_file);
			cpu_free(&cpu);
			return 1;
		}
	}
	if(data_specified) {
		if(cpu_load_data_bin(&cpu, data_file)!=0) {
			fprintf(stderr, "警告: 无法加载数据文件: %s（数据区保持全零）\n", data_file);
		}
	}
	if(sigsetjmp(sigsegv_env, 1)==0) 
		cpu_run(&cpu);
	else {
		fprintf(stderr, "\nTrapped SIGSEGV. \n");
		exe_err(&cpu);
		cpu_free(&cpu);
		display_destroy(display_get_global());
		exit(EXIT_FAILURE);
	}
	cpu_free(&cpu);
	display_destroy(display_get_global());
	return 0;
}
