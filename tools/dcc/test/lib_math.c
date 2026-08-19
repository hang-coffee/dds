// lib_math.c - mathlib.h 的实现（定义头文件声明的函数与变量）
#include "mathlib.h"

int g_count = 0;
int g_base = 10;

int add(int a, int b) {
    return a + b;
}

int mul(int a, int b) {
    return a * b;
}

int square(int x) {
    return mul(x, x);
}
