/* t18.c - 指针自增/自减、指针复合赋值、指针比较、char* 双指针场景 */
/* expect A = 0x45B */
int main(void) {
    int arr[5];
    int *p;
    int r;
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 4;
    arr[3] = 8;
    arr[4] = 16;
    p = &arr[0];
    r = *p;               /* 1 */
    p++;                  /* arr[1] */
    r = r + *p;           /* +2 = 3 */
    p += 2;               /* arr[3] */
    r = r + *p;           /* +8 = 11 */
    p--;                  /* arr[2] */
    r = r + *p;           /* +4 = 15 */
    p = &arr[0];
    if (p == &arr[0]) r = r + 100;   /* 指针相等比较 */
    if (p != &arr[4]) r = r + 1000;
    return r;             /* 1115 = 0x45B */
}
