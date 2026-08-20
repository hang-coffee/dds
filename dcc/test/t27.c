/* t27.c - 自动查找 lib：include <calc.h>（仅原型），实现自动来自 lib/calc.c */
/* expect A = 0x2B */
#include <calc.h>

int main(void) {
    int r;
    r = calc_add(3, 4);        /* 7 */
    r = r + calc_mul(2, 5);    /* +10 = 17 */
    r = r + calc_pow2(6);      /* +36 = 53 */
    r = r - 10;                /* 43 */
    return r;                  /* 43 = 0x2B */
}
