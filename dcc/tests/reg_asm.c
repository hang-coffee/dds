/* __reg_ 寄存器直访 + __asm__ 内联汇编 */
int main(void) {
    int r;
    __reg_X = 42;
    __reg_X++;
    __reg_I = __reg_X;
    r = __reg_I;
    __reg_X += 7;
    r = r + __reg_X;
    __asm__("LET A, DWORD 1");
    __asm__("ADD DWORD A, 1");
    r = r + __reg_A;
    return r;
}
