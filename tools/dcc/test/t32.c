/* t32.c - 64 位 long：存储、字面量、加减（进位/借位）、比较 */
/* expect A = 0x3F */
int main(void) {
    long a, b;
    int r = 0;

    a = 0x100000000;              /* 64 位字面量（超出 int32 范围） */
    if (a == 0x100000000) r = r + 1;    /* 高字相等 + 低字相等 */

    a = 0xFFFFFFFF;
    a = a + 1;                    /* 低字进位 → 0x100000000 */
    if (a == 0x100000000) r = r + 2;

    a = a - 1;                    /* 借位 → 0xFFFFFFFF */
    if (a == 0xFFFFFFFF) r = r + 4;

    b = 5;
    a = 0xFFFFFFFF + b;           /* int 右操作数提升 → 0x100000004 */
    if (a == 0x100000004) r = r + 8;

    a = 0x100000000;
    a = a - 0xFFFFFFFF;           /* 1 */
    if (a == 1) r = r + 16;

    a = -1;                       /* long -1（int 字面量符号扩展） */
    if (a < 0) r = r + 32;        /* 有符号比较：高字为全 1 */

    return r;                     /* 1+2+4+8+16+32 = 63 = 0x3F */
}
