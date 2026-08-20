/* 指针、一维数组、指针算术、指针参数、下标 */
int sum_array(int *p, int n) {
    int i;
    int s;
    s = 0;
    for (i = 0; i < n; i = i + 1) {
        s = s + p[i];
    }
    return s;
}

int main(void) {
    int arr[5];
    int *p;
    int r;
    int i;
    for (i = 0; i < 5; i = i + 1) {
        arr[i] = i * 10;
    }
    p = &arr[0];
    r = *p;                 /* 0 */
    p = p + 1;
    r = r + *p;             /* 10 */
    p++;
    r = r + *p;             /* 30 */
    p += 2;                 /* arr[4] */
    r = r + *p;             /* 70 */
    r = r + sum_array(arr, 5);  /* +100 = 170 */
    r = r + *(&arr[3]);     /* +30 = 200 */
    return r;
}
