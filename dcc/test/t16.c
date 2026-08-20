/* t16.c - signed char：符号扩展、有符号比较、signed/unsigned 混合 */
/* expect A = 0x2B67 */
int main(void) {
    signed char sc;
    unsigned char uc;
    int r;
    sc = -5;              /* 0xFB */
    uc = 200;
    r = 0;
    if (sc < 0) r = r + 1;         /* 有符号比较：-5 < 0 真 */
    if (sc > -10) r = r + 10;      /* -5 > -10 真 */
    if (uc > 100) r = r + 100;     /* 无符号：200 > 100 真 */
    if (uc < 250) r = r + 1000;    /* 真 */
    sc = sc + 1;          /* -4 */
    if (sc == -4) r = r + 10000;
    return r;             /* 1+10+100+1000+10000 = 11111 = 0x2B67 */
}
