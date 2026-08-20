// mathlib.h - 声明（函数原型 + extern 变量）
// 实现在 lib_math.c；用法: dcc main.c lib_math.c out.asm
#ifndef MATHLIB_H
#define MATHLIB_H

extern int g_count;          /* 由 lib_math.c 定义 */
extern int g_base;

int add(int a, int b);
int mul(int a, int b);
int square(int x);

#endif /* MATHLIB_H */
