/* t22.c - #include <mylib.h> 与 #define（对象宏 + 函数宏） */
/* expect A = 0x1E */
#include <mylib.h>
#define N 5
#define SQUARE(x) ((x) * (x))
#define ADD3(a, b, c) ((a) + (b) + (c))

int main(void) {
    int arr[N];
    int i;
    int s;
    for (i = 0; i < N; i = i + 1) {
        arr[i] = i;
    }
    s = MAX(3, 7);          /* 7 */
    s = s + MIN(10, 4);     /* +4 = 11 */
    s = s + SQUARE(3);      /* +9 = 20 */
    s = s + ABS(-5);        /* +5 = 25 */
    s = s + ADD3(1, 2, 3);  /* +6 = 31 */
    s = s + arr[N - 1];     /* +4 = 35 */
    s = s - N;              /* -5 = 30 */
    return s;               /* 30 = 0x1E */
}
