# TODO — 尚未移植的 C99 特性

> 当前 `dcc-c99` 只是最小 C99 重写，以下特性尚未移植。

## 类型系统

- [ ] `short` / `long` / `long long`
- [ ] `unsigned` / `signed` 限定符
- [ ] `float` / `double` / `long double`
- [ ] `_Bool`
- [ ] `const` / `volatile` / `restrict`
- [ ] `struct` / `union` / `enum`
- [ ] `typedef`
- [ ] 指针、数组、多维数组
- [ ] 函数指针
- [ ] `void *`
- [ ] 显式类型转换
- [ ] `sizeof`
- [ ] 字符串字面量 / `char *`
- [ ] 数组初始化列表 `{ ... }`

## 语句 / 语法

- [ ] `switch` / `case` / `default`
- [ ] `do ... while`
- [ ] `goto` / 标号
- [ ] 三目运算符 `?:`
- [ ] 逗号表达式
- [ ] 可变参数 `...`
- [ ] `inline`
- [ ] `_Static_assert`
- [ ] 匿名结构体 / 联合体

## 表达式

- [ ] 指针算术
- [ ] 下标 `a[i]`
- [ ] 成员访问 `.` / `->`
- [ ] `&` 取地址 / `*` 解引用
- [ ] 指针自增/自减
- [ ] `_Generic`

## 预处理 / 标准库

- [ ] `#include`
- [ ] `#define` / 宏
- [ ] `#if` / `#ifdef` / `#elif` / `#else` / `#endif`
- [ ] `#pragma` / `#error`
- [ ] 标准库头文件

## 代码生成 / 工具链

- [ ] ELF 目标文件输出
- [ ] 多文件编译
- [ ] 内联汇编
- [ ] 优化
- [ ] 调试信息
- [ ] 与现有 `dcc/` 功能对齐
