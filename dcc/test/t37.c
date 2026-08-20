/* t37.c - long 复合赋值与自增自减 */
/* expect A = 0x7F */
int main(void) {
    long a;
    int r = 0;

    a = 1;
    a += 0xFFFFFFFF;              /* 0x100000000 */
    if (a == 0x100000000) r = r + 1;

    a -= 2;                       /* 0xFFFFFFFE */
    if (a == 0xFFFFFFFE) r = r + 2;

    a *= 3;                       /* 0x2FFFFFFFA */
    if (a == 0x2FFFFFFFA) r = r + 4;

    a /= 2;                       /* 0x17FFFFFFD */
    if (a == 0x17FFFFFFD) r = r + 8;

    a %= 7;                       /* 0x17FFFFFFD % 7 = 3 */
    if (a == 3) r = r + 16;

    a = 0xFFFFFFFF;
    a++;                          /* 进位 → 0x100000000 */
    if (a == 0x100000000) r = r + 32;

    a--;                          /* 借位 → 0xFFFFFFFF */
    if (a == 0xFFFFFFFF) r = r + 64;

    return r;                     /* 1+2+4+8+16+32+64 = 127 = 0x7F */
}
