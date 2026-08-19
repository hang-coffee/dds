/* t20.c - __reg_ 寄存器直访（A/B/C/D1/D2/X/I，int 左值/右值） */
/* expect A = 0x33 */
int main(void) {
    int a;
    int r;
    a = 42;
    __reg_X = a;          /* X = 42 */
    __reg_X++;            /* X = 43 */
    __reg_I = __reg_X;    /* I = 43 */
    r = __reg_I;          /* r = 43 */
    __reg_X += 7;         /* X = 50 */
    r = r + __reg_X;      /* r = 93 */
    __reg_C = r;          /* C = 93 */
    r = __reg_C - 50;     /* r = 43 */
    a = __reg_X++;        /* a = 50, X = 51 */
    a = a + __reg_X;      /* a = 101 */
    r = r + a;            /* r = 144 */
    r = r - 93;           /* r = 51 */
    r = r + (++__reg_X);  /* ++X → 52, r = 103 */
    r = r - 52;           /* r = 51 */
    return r;             /* 51 = 0x33 */
}
