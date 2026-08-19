#include <stdint.h>
#include <stddef.h>
#include <io.h>

int fibonacci(int n);
void put_c(char c);
put_str(char *str);
void init_intr();
void num2str(unsigned int num, char *str);

void blank_isr(void) __interrupt__;
void init_ict_unit(struct Ict_unit *i) {
	i->addr=blank_isr;
	i->attr=0;
}

struct Ict_unit {
	uint32_t addr;
	uint32_t attr;
};

struct Ict_unit ict[256];

void blank_isr(void) {
	return;
}

int fibonacci(int n) {
	if(n==0 || n==1) return 1;
	return fibonacci(n-1)+fibonacci(n-2);
}

void put_c(char c) {
	outb(0x16, c);
	return;
}

void put_str(char *str) {
	int i=0;
	while(str[i]!=0) {
		put_c(str[i]);
		i++;
	}
	return;
}

void init_intr() {
	int i=0;
	while(i<256) {
		init_ict_unit(&(ict[i]));
		i++;
	}
	__asm__ (
			"PUSH DWORD A\n"
			"PUSH DWORD X\n");
	__reg_X=&ict;
	__asm__ (
			"SETB ICTB, X\n"
			"PUSH DWORD 0xffffffff\n"
			"POP RIN1\n"
			"POP DWORD X\n"
			"POP DWORD A\n"
			);
	return;
}

void num2str(unsigned int num, char *str)
{
    char buffer[16];      // 临时倒序缓冲区（32位最大 4294967295，10位足够，16绝对安全）
    int i = 0;
    int j = 0;

    // 特殊情况：如果数字是 0，直接返回 "0"
    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }

    // 1. 从低位到高位逐位取出，存入 buffer（此时是倒序）
    while (num > 0) {
        buffer[i] = '0' + (num % 10);  // 取个位数字转为 ASCII
        num = num / 10;                // 去掉个位
        i++;
    }

    // 2. 将 buffer 倒序复制回 str
    while (i > 0) {
        i--;               // 先减，指向最后一个有效字符
        str[j] = buffer[i];
        j++;
    }
    str[j] = '\0';         // 添加字符串结束符
}


int main() {
	outd(0x18, 0x01);
	char s[16];
	num2str(fibonacci(8), s);
	init_intr();
	put_str("Hello World!\n");
	put_str(s);
	return 0;
}

