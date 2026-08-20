// input.c - 宿主键盘输入 → KBC 设备
//
// 流程:
//   input_init():  stdin 为终端时进入 raw 模式（关闭 ICANON/ECHO/ISIG），
//                  使 Ctrl+C 等作为字符到达而非信号；注册 atexit 恢复终端。
//   input_poll():  cpu_run 每轮调用，非阻塞读取 stdin 并处理:
//                    0x03 (Ctrl+C)  安全键: 切换暂停状态
//                    暂停时 'd'      显示寄存器转储（exe_err）
//                    暂停时 'q'      退出模拟器
//                    暂停时其它键    不转发
//                    普通键          ASCII → Set 1 make 码 → kbc_inject_scancode
//                                   （方向键等经 ESC [ 转义序列 → E0 扩展码）
//
// 注入为 make 事件（按键按下）；break 码与长按重复不模拟。

#include "input.h"
#include "devices/kbc.h"
#include "debugger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>

volatile bool sim_paused=false;
volatile unsigned long sim_steps_remaining=0;

static char pause_line[64];
static size_t pause_len=0;

static struct termios saved_tio;
static bool tty_active=false;

static void input_restore(void) {
	if(tty_active) tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio);
}


void input_pause_prompt(void) {
	fprintf(stderr, "? ");
	fflush(stderr);
}

static void pause_reset_line(void) {
	pause_len=0;
	pause_line[0]='\0';
}

static void pause_handle_command(DOCTOR_CPU *cpu, const char *cmd) {
	while(*cmd && isspace((unsigned char)*cmd)) cmd++;
	if(*cmd=='\0') return;

	// stack
	if(strncmp(cmd, "stack", 5)==0 && (cmd[5]=='\0' || isspace((unsigned char)cmd[5]))) {
		dump_stack(cpu);
		return;
	}

	// d / dump
	if((cmd[0]=='d' || cmd[0]=='D') && (cmd[1]=='\0' || isspace((unsigned char)cmd[1]))) {
		fprintf(stderr, "\n[KBD] 寄存器转储:\n");
		exe_err(cpu);
		fprintf(stderr, "\n");
		return;
	}

	// q / quit
	if((cmd[0]=='q' || cmd[0]=='Q') && (cmd[1]=='\0' || isspace((unsigned char)cmd[1]))) {
		fprintf(stderr, "\n[KBD] 退出\n");
		input_restore();
		exit(0);
	}

	// s / step [num]
	if((cmd[0]=='s' || cmd[0]=='S') && (cmd[1]=='\0' || isspace((unsigned char)cmd[1]))) {
		const char *p=cmd+1;
		while(*p && isspace((unsigned char)*p)) p++;
		unsigned long n=1;
		if(*p!='\0') {
			char *end=NULL;
			errno=0;
			n=strtoul(p, &end, 0);
			if(errno!=0 || end==p) n=1;
		}
		sim_steps_remaining=n;
		fprintf(stderr, "Step %lu.\n", n);
		return;
	}

	fprintf(stderr, "未知命令: %s\n", cmd);
}

void input_init(void) {
	if(!isatty(STDIN_FILENO)) return;			// 非终端（测试/管道）不启用
	if(tcgetattr(STDIN_FILENO, &saved_tio)!=0) return;
	struct termios raw=saved_tio;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG);
	raw.c_iflag &= ~(IXON | ICRNL | ISTRIP);
	raw.c_cc[VMIN]=0;
	raw.c_cc[VTIME]=0;							// 非阻塞
	if(tcsetattr(STDIN_FILENO, TCSANOW, &raw)!=0) return;
	tty_active=true;
	atexit(input_restore);
	fprintf(stderr, "[KBD] 键盘输入已连接 (Ctrl+C 暂停/恢复, 暂停时 d/s/stack/q)\n");
	return;
}

// 注入按键事件: make + break（Shift 修饰时先 Shift make、后 Shift break）
static void kbd_send_key(DOCTOR_CPU *cpu, uint8_t make, bool need_shift) {
	if(need_shift) kbc_inject_scancode(cpu, 0x2A);		// LShift make
	kbc_inject_scancode(cpu, make);
	kbc_inject_scancode(cpu, make | 0x80);
	if(need_shift) kbc_inject_scancode(cpu, 0xAA);		// LShift break
}

static void kbd_send_e0(DOCTOR_CPU *cpu, uint8_t code) {
	kbc_inject_scancode(cpu, 0xE0);
	kbc_inject_scancode(cpu, code);
	kbc_inject_scancode(cpu, 0xE0);
	kbc_inject_scancode(cpu, code | 0x80);
}

// ASCII 字符 → Set 1 make 码 + 是否需要 Shift 修饰
static uint8_t ascii_to_set1(char c, bool *need_shift) {
	unsigned char ch=(unsigned char)c;
	*need_shift=false;
	if(ch>='A' && ch<='Z') { *need_shift=true; ch=(unsigned char)(ch+32); }
	switch(ch) {
		case '!': *need_shift=true; ch='1'; break; case '@': *need_shift=true; ch='2'; break;
		case '#': *need_shift=true; ch='3'; break; case '$': *need_shift=true; ch='4'; break;
		case '%': *need_shift=true; ch='5'; break; case '^': *need_shift=true; ch='6'; break;
		case '&': *need_shift=true; ch='7'; break; case '*': *need_shift=true; ch='8'; break;
		case '(': *need_shift=true; ch='9'; break; case ')': *need_shift=true; ch='0'; break;
		case '_': *need_shift=true; ch='-'; break; case '+': *need_shift=true; ch='='; break;
		case '{': *need_shift=true; ch='['; break; case '}': *need_shift=true; ch=']'; break;
		case '|': *need_shift=true; ch='\\'; break;
		case ':': *need_shift=true; ch=';'; break; case '"': *need_shift=true; ch='\''; break;
		case '<': *need_shift=true; ch=','; break; case '>': *need_shift=true; ch='.'; break;
		case '?': *need_shift=true; ch='/'; break;
		case '~': *need_shift=true; ch='`'; break;
		default: break;
	}
	switch(ch) {
		case ' ':  return 0x39;
		case '`':  return 0x29;
		case '1':  return 0x02; case '2':  return 0x03; case '3':  return 0x04;
		case '4':  return 0x05; case '5':  return 0x06; case '6':  return 0x07;
		case '7':  return 0x08; case '8':  return 0x09; case '9':  return 0x0A;
		case '0':  return 0x0B;
		case '-':  return 0x0C; case '=':  return 0x0D;
		case 'q':  return 0x10; case 'w':  return 0x11; case 'e':  return 0x12;
		case 'r':  return 0x13; case 't':  return 0x14; case 'y':  return 0x15;
		case 'u':  return 0x16; case 'i':  return 0x17; case 'o':  return 0x18;
		case 'p':  return 0x19; case '[':  return 0x1A; case ']':  return 0x1B;
		case '\\': return 0x2B;
		case 'a':  return 0x1E; case 's':  return 0x1F; case 'd':  return 0x20;
		case 'f':  return 0x21; case 'g':  return 0x22; case 'h':  return 0x23;
		case 'j':  return 0x24; case 'k':  return 0x25; case 'l':  return 0x26;
		case ';':  return 0x27; case '\'': return 0x28;
		case 'z':  return 0x2C; case 'x':  return 0x2D; case 'c':  return 0x2E;
		case 'v':  return 0x2F; case 'b':  return 0x30; case 'n':  return 0x31;
		case 'm':  return 0x32;
		case ',':  return 0x33; case '.':  return 0x34; case '/':  return 0x35;
		default:   return 0;
	}
}

// 处理单个按键字节（含 ESC 转义序列状态机）
// esc_state: 0=普通, 1=已收 ESC, 2=已收 ESC [, 3=已收 ESC O
static void handle_key(DOCTOR_CPU *cpu, uint8_t ch) {
	static int esc_state=0;

	if(esc_state==1) {
		if(ch=='[') { esc_state=2; return; }
		if(ch=='O') { esc_state=3; return; }
		esc_state=0;
		kbd_send_key(cpu, 0x01, false);			// 裸 ESC 键
		return;
	}
	if(esc_state==2) {
		esc_state=0;
		switch(ch) {					// 方向键 → E0 扩展
			case 'A': kbd_send_e0(cpu, 0x48); break;	// Up
			case 'B': kbd_send_e0(cpu, 0x50); break;	// Down
			case 'C': kbd_send_e0(cpu, 0x4D); break;	// Right
			case 'D': kbd_send_e0(cpu, 0x4B); break;	// Left
			default: break;
		}
		return;
	}
	if(esc_state==3) {
		esc_state=0;
		switch(ch) {					// ESC O P/Q/R/S → F1-F4
			case 'P': kbd_send_key(cpu, 0x3B, false); break;
			case 'Q': kbd_send_key(cpu, 0x3C, false); break;
			case 'R': kbd_send_key(cpu, 0x3D, false); break;
			case 'S': kbd_send_key(cpu, 0x3E, false); break;
			default: break;
		}
		return;
	}

	// 普通状态
	if(ch==0x03) {// 安全键 Ctrl+C
		sim_paused=!sim_paused;
		if(sim_paused) {
			pause_reset_line();
			sim_steps_remaining=0;
			fprintf(stderr, "\n[KBD] 暂停（Ctrl+C 恢复，q 退出）\n");
			input_pause_prompt();
		} else {
			sim_steps_remaining=0;
			fprintf(stderr, "\n[KBD] 继续\n");
		}
		return;
	}
	if(sim_paused) {
		// 暂停时立即处理单字符命令（兼容旧的 d/q 操作）
		if(pause_len==0 && (ch=='d' || ch=='D')) {
			fprintf(stderr, "\n[KBD] 寄存器转储:\n");
			exe_err(cpu);
			fprintf(stderr, "\n");
			input_pause_prompt();
			return;
		}
		if(pause_len==0 && (ch=='q' || ch=='Q')) {
			fprintf(stderr, "\n[KBD] 退出\n");
			input_restore();
			exit(0);
		}

		if(ch=='\r' || ch=='\n') {
			pause_line[pause_len]='\0';
			fprintf(stderr, "\n");
			pause_handle_command(cpu, pause_line);
			pause_reset_line();
			// 如果还有剩余单步步数，等 cpu_run 执行完后再显示提示符
			if(sim_steps_remaining==0) {
				input_pause_prompt();
			}
			return;
		}
		if(ch==8 || ch==127) {// Backspace
			if(pause_len>0) {
				pause_len--;
				pause_line[pause_len]='\0';
				fprintf(stderr, "\b \b");
				fflush(stderr);
			}
			return;
		}
		if(ch>=32 && ch<127) {
			if(pause_len < sizeof(pause_line)-1) {
				pause_line[pause_len++]=ch;
				pause_line[pause_len]='\0';
				fputc(ch, stderr);
				fflush(stderr);
			}
			return;
		}
		return;// 暂停期间其它控制键不转发
	}
	switch(ch) {
		case 0x0D: kbd_send_key(cpu, 0x1C, false); return;		// Enter
		case 0x09: kbd_send_key(cpu, 0x0F, false); return;		// Tab
		case 0x08: case 0x7F: kbd_send_key(cpu, 0x0E, false); return;	// Backspace
		case 0x1B: esc_state=1; return;				// ESC（可能为转义序列）
		default: break;
	}
	bool need_shift=false;
	uint8_t make=ascii_to_set1((char)ch, &need_shift);
	if(make) kbd_send_key(cpu, make, need_shift);
	return;
}

void input_poll(DOCTOR_CPU *cpu) {
	if(!tty_active) return;
	uint8_t buf[16];
	ssize_t n=read(STDIN_FILENO, buf, sizeof(buf));
	if(n<=0) return;
	for(ssize_t i=0; i<n; i++) handle_key(cpu, buf[i]);
	return;
}
