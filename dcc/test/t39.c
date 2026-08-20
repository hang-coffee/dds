/* t39.c - long 函数参数与返回值（8 字节传参/返回） */
/* expect A = 0x3F */
long addl(long a, long b) { return a + b; }
long negl(long a) { return -a; }
unsigned long divl(unsigned long a, unsigned long b) { return a / b; }

int main(void) {
    int r = 0;
    long s;

    s = addl(0x100000000, 1);     /* 0x100000001 */
    if (s == 0x100000001) r = r + 1;

    s = addl(0xFFFFFFFF, 2);      /* 0x100000001（int 实参提升） */
    if (s == 0x100000001) r = r + 2;

    s = negl(-5);                 /* 5 */
    if (s == 5) r = r + 4;

    s = divl(0x100000000, 3);     /* 0x55555555 */
    if (s == 0x55555555) r = r + 8;

    s = addl(s, s);               /* 0xAAAAAAAA */
    if (s == 0xAAAAAAAA) r = r + 16;

    s = negl(s);                  /* -0xAAAAAAAA（负） */
    if (s < 0) r = r + 32;

    return r;                     /* 63 = 0x3F */
}
