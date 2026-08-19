/* t4.c - int 数组、下标赋值/读取、循环求和 */   /* expect A = 0x64 */
int main(void) {
    int arr[5];
    int i;
    int s;
    for (i = 0; i < 5; i = i + 1) {
        arr[i] = i * 10;
    }
    s = 0;
    for (i = 0; i < 5; i = i + 1) {
        s = s + arr[i];
    }
    return s;          /* 0+10+20+30+40 = 100 */
}
