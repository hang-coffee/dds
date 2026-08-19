/* t3.c - char 类型、字符字面量、比较链 */   /* expect A = 0x65 */
int main(void) {
    char c;
    int r;
    c = 'A';           /* 65 */
    if (c == 'A') r = 100;
    else r = 200;
    c = c + 1;         /* 'B' = 66 */
    if (c > 'A') r = r + 1;
    return r;          /* 101 */
}
