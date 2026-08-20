/* t43.c - 新增 __reg_S/E/R/F/T 寄存器直访 */
/* expect A = 0x43 */
int main(void) {
    unsigned int s0 = __reg_S;
    unsigned int f0 = __reg_F;
    unsigned int e0 = __reg_E;
    __reg_T = 0x1234;
    __reg_R = __reg_T;
    if (__reg_R != 0x1234) return 0;
    if (e0 == e0 && s0 == s0 && f0 == f0)
        return 0x43;
    return 0;
}
