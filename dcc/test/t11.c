/* t11.c - 嵌套循环、负数、MNE、比较链 */   /* expect A = 0x455 */
int main(void) {
    int i;
    int j;
    int s;
    int n;
    s = 0;
    for (i = 0; i < 3; i = i + 1) {
        for (j = 0; j < 3; j = j + 1) {
            s = s + 1;      /* 9 次 */
        }
    }
    n = -5;                 /* MNE */
    if (n < 0 && n > -10) s = s + 100;
    if (n == -5) s = s + 1000;
    if (n != -5) s = 0;
    return s;               /* 9 + 1100 = 1109 */
}
