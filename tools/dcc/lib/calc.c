// calc.c - calc.h 的实现（位于 tools/dcc/lib/，编译器自动查找）
#include <calc.h>

int calc_add(int a, int b) {
    return a + b;
}

int calc_mul(int a, int b) {
    return a * b;
}

int calc_pow2(int x) {
    return x * x;
}
