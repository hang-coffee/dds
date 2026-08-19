/* t26.c - __reg_ 类型为 unsigned int：无符号右移/比较/除法/取余 */
/* expect A = 0x2B03 */
int main(void) {
    int r;
    r = 0;
    __reg_X = 0xFFFFFFF0;         /* 大无符号数 */
    __reg_X = __reg_X >> 4;       /* 逻辑右移 = 0x0FFFFFFF */
    if (__reg_X == 0x0FFFFFFF) r = r + 1;
    if (__reg_X > 0x0FFFFFF0) r = r + 10;   /* 无符号比较：真 */
    if (__reg_X < 0) r = r + 100;           /* 无符号：假（0x0FFFFFFF > 0） */
    __reg_X = 100;
    __reg_X = __reg_X / 3;        /* 无符号除 = 33 */
    if (__reg_X == 33) r = r + 1000;
    __reg_X = 17;
    __reg_X %= 5;                 /* 无符号取余 = 2 */
    if (__reg_X == 2) r = r + 10000;
    return r;                     /* 1+10+1000+10000 = 11011 = 0x2B0B */
}
