#include <stdint.h>
#include <stdbool.h>
#include "_reg.h"

static inline uint8_t inb(uint16_t port) {
	__asm__("PUSH DWORD A\n"
			"PUSH DWORD X");
	__reg_A=(uint32_t)port;
	__asm__("IN BYTE X, A");
	uint8_t ret=(uint8_t)__reg_X;
	__asm__("POP DWORD X\n"
			"POP DWORD A");
	return ret;
}

static inline uint16_t inw(uint16_t port) {
	__asm__("PUSH DWORD A\n"
			"PUSH DWORD X");
	__reg_A=(uint32_t)port;
	__asm__("IN WORD X, A");
	uint16_t ret=(uint16_t)__reg_X;
	__asm__("POP DWORD X\n"
			"POP DWORD A");
	return ret;
}

static inline uint32_t ind(uint16_t port) {
	__asm__("PUSH DWORD A\n"
			"PUSH DWORD X");
	__reg_A=(uint32_t)port;
	__asm__("IN DWORD X, A");
	uint32_t ret=__reg_X;
	__asm__("POP DWORD X\n"
			"POP DWORD A");
	return ret;
}

static inline void outb(uint16_t port, uint8_t data) {
	__asm__("PUSH DWORD A\n"
			"PUSH DWORD X");
	__reg_A=(uint32_t)port;
	__reg_X=(uint32_t)data;
	__asm__("OUT BYTE A, X\n"
			"POP DWORD X\n"
			"POP DWORD A");
	return;
}

static inline void outw(uint16_t port, uint16_t data) {
	__asm__("PUSH DWORD A\n"
			"PUSH DWORD X");
	__reg_A=(uint32_t)port;
	__reg_X=(uint32_t)data;
	__asm__("OUT WORD A, X\n"
			"POP DWORD X\n"
			"POP DWORD A");
	return;
}

static inline void outd(uint16_t port, uint32_t data) {
	__asm__("PUSH DWORD A\n"
			"PUSH DWORD X");
	__reg_A=(uint32_t)port;
	__reg_X=data;
	__asm__("OUT DWORD A, X\n"
			"POP DWORD X\n"
			"POP DWORD A");
	return;
}

void blank_isr(void) __interrupt__;
void disk_isr(void) __interrupt__;
void disk_init();
void uart_init();
void uart_puts(const char *str);

void wait_disk_ready();

struct ict_gate {
    uint32_t addr;
    uint32_t attr;
};


struct ict_gate ict[256];
int sector_cnt;
int sector_size;
uint8_t disk_buf[4096];
bool diskok;

void main() {
    uart_init();
    // 初始化中断
    int i;
    for(i=0; i<256; i++) {
        ict[i].addr=blank_isr;
        ict[i].attr=0;
    }
    __asm__(
        "PUSH DWORD A");
    __reg_A=(uint32_t)ict;
    __asm__(
        "SETB ICTB, A\n"
        "LET DWORD A, 0x80080000\n"
        "SETB RIN3_CTRL, A\n"
        "PUSHI\n"
        "PUSH DWORD 0xffffffff\n"
        "POP RIN1\n"
        "POPI\n"
        "POP DWORD A\n"
    );
    // 初始化DISK设备
    disk_init();
    // 读盘：读最开头的4KB
    uint32_t sec_cnt_read;
    sec_cnt_read=4096/sector_size;
    if((4096%sector_size)!=0) sec_cnt_read++;
    diskok=0;
    outd(0x27, 0);
    outd(0x28, 0);      // LBA=0
    outd(0x29, sec_cnt_read);  // 读扇区数
    outd(0x2c, disk_buf);   // 缓冲区地址
    outd(0x2a, 0x01);   // 读
    wait_disk_ready();
    __asm__(
        "PUSH DWORD A\n"
        "PUSH DWORD E\n"
        "PUSH DWORD C");
    __reg_A=(uint32_t)disk_buf;
    __asm__(
        "LET DWORD C, 1024\n"
        "PUSH DWORD 0x1000\n"
        ".Lloop_copy:\n"
        "POP DWORD E\n"
        "TRA DWORD *E, *A\n"
        "ADD DWORD A, 4\n"
        "ADD DWORD E, 4\n"
        "CDI\n"
        "PUSH DWORD E\n"
        "LET DWORD E, .Lloop_copy_fin\n"
        "JZ\n"
        "LET DWORD E, .Lloop_copy\n"
        "JMP\n"
        ".Lloop_copy_fin:\n"
        "POP DWORD E\n"
        "POP DWORD C\n"
        "POP DWORD E\n"
        "POP DWORD A"
    );
    outd(0x27, sec_cnt_read); // 这是LBA低位
    outd(0x28, 0);      // 这是LBA高位，LBA=sec_cnt_read
    outd(0x29, sec_cnt_read);   // 再读4KB
    outd(0x2c, disk_buf);
    outd(0x2a, 0x01);   // 读
    wait_disk_ready();
    uint8_t *p=(uint8_t *)0x4000;
    for(i=0; i<4096; i++) {
        p[i]=disk_buf[i];
    }
    // 执行之后，0x4000中就是磁盘引导程序的数据区了，代码区已经被加载
    // 跳转到磁盘引导程序的入口点
    __asm__(
        "LET DWORD E, 0x1000\n"
        "JMP"
    );
    return;
}

void blank_isr(void) {
    return;
}

void disk_init() {
    uint32_t tot_sec;
    tot_sec=ind(0x2f);
    if(tot_sec==0) {
        // 没有磁盘
        uart_puts("Disk not found\n");
        while(1) {
            __asm__("HLT");
        }        
    }
    __asm__("PUSHI");
    ict[13].addr=disk_isr;  // 安装ISR
    __asm__("POPI");
    outd(0x2c, 0);
    outd(0x2d, 0x3); // DISK设备+中断使能
    outd(0x2a, 0xff); // 复位
    sector_cnt=tot_sec;
    sector_size=ind(0x2e);
    if(4096/sector_size > sector_cnt) {
        uart_puts("Disk too small\n");
        while(1) {
            __asm__("HLT");
        }
    }
    return;
}

void disk_isr(void) {
    uint8_t stat=inb(0x2b);
    if((stat&0x8) && !(stat&0x1)) {
        // 上次的命令完成了
        diskok=1;
    }
    return;
}

void uart_init() {
    outw(0x18, 0x01);   // 设备使能
    return;
}

void uart_puts(const char *str) {
    int i;
    i=0;
    while(str[i]!='\0') {
        outb(0x16, str[i]);
        i++;
    }
    return;
}

void wait_disk_ready() {
halt_until_read_ok:
    if(diskok==1) {
        diskok=0;
        goto next_step;
    }
    __asm__("HLT");
    goto halt_until_read_ok;
next_step:
//    uart_puts("ok\n");
//    if(disk_buf[1023]!=0x55 || disk_buf[1022]!=0xaa) {
//        uart_puts("Disk signature error\n");
//        while(1) {
//            __asm__("HLT");
//        }
//    }
    return;
}
