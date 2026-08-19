/* test1.c - dcc 冒烟测试 1：函数、参数、局部变量、循环、算术 */   /* expect A = 0x2D */

int sum(int n) {
    int i;
    int s;
    s = 0;
    for (i = 0; i < n; i = i + 1) {
        s = s + i;
    }
    return s;
}

int main(void) {
    int r;
    r = sum(10);        /* 0+1+...+9 = 45 */
    return r;
}
