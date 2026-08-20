/* t31.c - __interrupt__ ISR：原型修饰、return 编为 IRET、软件中断触发 */
/* expect A = 0x2A */
void pit_isr(void) __interrupt__;
int count;

void pit_isr(void) {
    count = count + 1;
    return;              /* 编译为 MOV S,F; POP F; IRET */
}

int main(void) {
    count = 0;
    /* 配置 ICT 表项 0x10 → func_pit_isr（ictb + 8*0x10 = 0x1100） */
    __asm__("LET R, DWORD 0x1100");
    __asm__("LET A, DWORD func_pit_isr");
    __asm__("STO DWORD A");
    __asm__("LET A, DWORD 0x10");   /* ISR_NMO=1 */
    __asm__("STO BYTE A");
    /* ICTB = 0x1080 */
    __asm__("LET R, DWORD 0x1080");
    __asm__("SETB ICTB, R");
    /* 软件中断 INT 0x10 → handle_intr → ISR → IRET */
    __asm__("INT BYTE 0x10");
    return count + 41;   /* 1 + 41 = 42 */
}
