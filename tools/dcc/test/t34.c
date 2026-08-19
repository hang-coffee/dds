/* t34.c - 64 位 long 除法/取余（有符号与无符号） */
/* expect A = 0x3F */
int main(void) {
    long a, b;
    unsigned long ua, ub;
    int r = 0;

    a = 0x100000000;
    b = 3;
    a = a / b;                    /* 0x55555555 */
    if (a == 0x55555555) r = r + 1;

    a = 0x100000000;
    a = a % 3;                    /* 1 */
    if (a == 1) r = r + 2;

    a = -100;
    b = 7;
    a = a / b;                    /* -14（向零截断） */
    if (a == -14) r = r + 4;

    a = -100;
    a = a % 7;                    /* -2（余数符号同被除数） */
    if (a == -2) r = r + 8;

    ua = 0x100000000;
    ub = 3;
    ua = ua / ub;                 /* 0x55555555 */
    if (ua == 0x55555555) r = r + 16;

    ua = 0xFFFFFFFF;
    ub = 0xFFFFFFFF;
    ua = ua / ub;                 /* 1 */
    if (ua == 1) r = r + 32;

    return r;                     /* 63 = 0x3F */
}
