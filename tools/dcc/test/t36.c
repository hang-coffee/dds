/* t36.c - long 比较、一元、条件与短路 */
/* expect A = 0xFF */
int main(void) {
    long a, b;
    int r = 0;

    a = 0x100000000;
    b = 0xFFFFFFFF;
    if (a > b) r = r + 1;         /* 高字定大小 */
    if (b < a) r = r + 2;

    a = -1;
    b = 1;
    if (a < b) r = r + 4;         /* 有符号比较 */
    if (b > a) r = r + 8;
    if (a == -1) r = r + 16;
    if (a != 1) r = r + 32;

    a = 0;
    if (!a) r = r + 64;           /* !long */

    if (a || (b && a)) r = r + 0; /* 短路：a 假、b 真、a 假 → 假 */
    else r = r + 128;

    return r;                     /* 1+2+4+8+16+32+64+128 = 255 = 0xFF */
}
