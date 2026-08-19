/* t21.c - 强制类型转换 (int)/(char)/(unsigned) */
/* expect A = 0x5E */
int main(void) {
    char c;
    unsigned int u;
    int s;
    int r;
    c = 'k';              /* 107 */
    r = (int)c;           /* 107 */
    r = r + (char)300;    /* (char)300 = 44 → 151 */
    r = r + (char)256;    /* (char)256 = 0 → 151 */
    u = 0xFFFFFFF0;       /* 无符号大数 */
    s = (int)u;           /* 重解释为 int = -16 */
    r = r + s;            /* 151-16 = 135 */
    r = r + (unsigned char)255;  /* 255 → 390? 不，r=135+255=390 */
    r = r - 255;          /* 135 */
    r = r - 41;           /* 94 */
    return r;             /* 94 = 0x5E */
}
