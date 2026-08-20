/* t25.c - 分离编译：声明在 mathlib.h，实现在 lib_math.c */
/* sources: lib_math.c */
/* expect A = 0x1E */
#include "mathlib.h"

int main(void) {
    int r;
    g_count = 3;               /* extern 变量（lib_math.c 定义） */
    r = add(g_count, 4);       /* 7 */
    r = r + mul(2, 5);         /* +10 = 17 */
    r = r + square(3);         /* +9 = 26 */
    r = r + g_base;            /* +10 = 36 */
    r = r - g_count;           /* -3 = 33 */
    r = r - 3;                 /* 30 */
    return r;                  /* 30 = 0x1E */
}
