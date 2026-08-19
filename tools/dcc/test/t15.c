/* t15.c - 字符串：char* 指针、字符串参数、下标访问、strlen 模拟 */
/* expect A = 0xD5 */
int mylen(char *s) {
    int n;
    n = 0;
    while (s[n] != 0) {
        n = n + 1;
    }
    return n;
}

int main(void) {
    char *p;
    int r;
    p = "hello";            /* p 指向字符串常量 */
    r = p[1];               /* 'e' = 101 */
    r = r + mylen(p);       /* +5 = 106 */
    r = r + mylen("abc");   /* +3 = 109 */
    r = r + p[0];           /* +104 = 213 */
    return r;               /* 213 = 0xD5 */
}
