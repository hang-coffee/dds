// boot.c - 启动的初始化代码
// 链接bootable-crt

#include <stdint.h>
#include "io.h"
#include "intr.h"
#include "disk.h"

void uart_puts(char *str) {
	int i=0;
	while(str[i]!='\0') {
		outb(0x16, str[i]);
		i++;
	}
	return;
}

int main() {
	outd(0x18, 0x01);
	uart_puts("Initializing interrupt...");
	init_intr();
	uart_puts("Done\nLoading disk...");
	init_disk();
	while(1) {
		__asm__("HLT");
	}
	return 0;
}

