/* t38.c - long 类型转换（截断/符号/零扩展） */
/* expect A = 0x3F */
int main(void) {
    long a;
    int i;
    unsigned long ua;
    int r = 0;

    i = -5;
    a = (long)i;                  /* 符号扩展 */
    if (a == -5) r = r + 1;

    ua = (unsigned long)i;        /* 零扩展：4294967291 */
    if (ua == 4294967291) r = r + 2;

    a = 0x123456789;
    i = (int)a;                   /* 截断低 32 位 = 0x23456789 */
    if (i == 0x23456789) r = r + 4;

    a = (long)0xFFFFFFFF;         /* 4294967295（正 long） */
    if (a == 4294967295) r = r + 8;

    a = (long)5;
    if (a == 5) r = r + 16;

    a = -1;
    if ((int)a == -1) r = r + 32;

    return r;                     /* 1+2+4+8+16+32 = 63 = 0x3F */
}
