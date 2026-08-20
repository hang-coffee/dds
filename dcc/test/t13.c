/* t13.c - unsigned 类型：无符号比较、无符号右移、无符号除法 */
/* expect A = 0x457 */
int main(void) {
    unsigned int ua;
    unsigned int vb;
    int r;
    ua = 4000000000;      /* 0xEE6B2800, 超过 int 上限 */
    vb = 100;
    if (ua > 3000000000) r = 1;   /* 无符号比较：真 */
    else r = 0;
    if (vb < 200) r = r + 10;      /* 真 */
    ua = 0x80000000;
    ua = ua >> 1;         /* 逻辑右移 = 0x40000000 */
    if (ua == 0x40000000) r = r + 100;
    ua = 1000;
    ua = ua / 7;          /* 142 */
    if (ua == 142) r = r + 1000;
    return r;             /* 1 + 10 + 100 + 1000 = 1111 */
}
