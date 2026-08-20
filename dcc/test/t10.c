/* t10.c - while、break、continue、移位、位运算、! */   /* expect A = 0x40 */
int main(void) {
    int i;
    int s;
    int x;
    i = 0;
    s = 0;
    while (i < 20) {
        i = i + 1;
        if (i % 2 == 0) continue;   /* 跳过偶数 */
        if (i > 10) break;          /* 11 停 */
        s = s + i;                  /* 1+3+5+7+9 = 25 */
    }
    x = 1;
    x = x << 4;                     /* 16 */
    x = x >> 1;                     /* 8 */
    x = x | 0x30;                   /* 0x38 = 56 */
    x = x & 0x2F;                   /* 0x28 = 40 */
    x = x ^ 0x0F;                   /* 0x27 = 39 */
    if (!(x == 39)) s = 0;
    return s + x;                   /* 25 + 39 = 64 */
}
