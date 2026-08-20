/* t33.c - 64 位 long 乘法（学校算法，低 64 位） */
/* expect A = 0x3F */
int main(void) {
    long a, b;
    int r = 0;

    a = 0x100000000;
    b = 2;
    a = a * b;                    /* 0x200000000 */
    if (a == 0x200000000) r = r + 1;

    a = 0xFFFFFFFF;
    b = 0xFFFFFFFF;
    a = a * b;                    /* (2^32-1)^2 = 0xFFFFFFFE00000001（超 int64 字面量上限，无符号移位检验高字） */
    if (((unsigned long)a >> 32) == 0xFFFFFFFE) r = r + 2;

    a = 0x123456789;
    b = 3;
    a = a * b;                    /* 0x369D0369B */
    if (a == 0x369D0369B) r = r + 4;

    a = 0x80000000;
    b = 0x80000000;
    a = a * b;                    /* 2^62 = 0x4000000000000000 */
    if (a == 0x4000000000000000) r = r + 8;

    a = 0x100000000;
    b = 0x100000000;
    a = a * b;                    /* 2^64 mod 2^64 = 0（截断） */
    if (a == 0) r = r + 16;

    a = -1;
    b = 1;
    a = a * b;                    /* -1 */
    if (a < 0) r = r + 32;

    return r;                     /* 1+2+4+8+16+32 = 63 = 0x3F */
}
