/* t14.c - 指针：& * 解引用、指针算术、指针参数、指针下标 */
/* expect A = 0x57 */
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
    int arr[4];
    int *ptr;
    int r;
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    arr[3] = 40;
    ptr = &arr[0];        /* 指向 arr[0] */
    r = *ptr;             /* 10 */
    ptr = ptr + 1;        /* 指向 arr[1] */
    r = r + *ptr;         /* 30 */
    r = r + sum_array(&arr[0], 4);   /* +100 = 130 */
    r = r - 100;          /* 30 */
    r = r + *(&arr[3]);   /* +40 = 70 */
    r = r + *(ptr - 1);   /* *(arr[0]) = 10 → 80 */
    r = r + 7;            /* 87 */
    return r;
}
