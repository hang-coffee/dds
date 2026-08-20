/* 多维数组：声明、赋值、读取、指针式访问 */
int main(void) {
    int a[2][3];
    int i;
    int j;
    int s;
    int *p;
    for (i = 0; i < 2; i = i + 1) {
        for (j = 0; j < 3; j = j + 1) {
            a[i][j] = i * 10 + j;
        }
    }
    s = 0;
    for (i = 0; i < 2; i = i + 1) {
        for (j = 0; j < 3; j = j + 1) {
            s = s + a[i][j];
        }
    }
    p = &a[1][0];
    s = s + p[2];           /* +5 */
    return s;
}
