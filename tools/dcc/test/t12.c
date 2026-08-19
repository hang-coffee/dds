/* t12.c - 多参数、char 参数/返回值 */   /* expect A = 0x5A */
int max4(int a, int b, int c, int d) {
    int m;
    m = a;
    if (b > m) m = b;
    if (c > m) m = c;
    if (d > m) m = d;
    return m;
}

char toUpper(char c) {
    if (c >= 'a' && c <= 'z') {
        c = c - 32;
    }
    return c;
}

int main(void) {
    int r;
    char ch;
    r = max4(3, 9, 5, 7);      /* 9 */
    ch = toUpper('q');         /* 'Q' = 81 */
    return r + ch;             /* 90 */
}
