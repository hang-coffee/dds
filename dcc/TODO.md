# TODO / 已知限制

> 当前 `dcc` 已经是功能更完整的 C99 重写版。以下清单区分“已完成”和“尚未支持”。

## 已完成的 C99 / 扩展特性

### 类型系统

- [x] `short` / `long` / `long long`（`long`/`long long` 使用 8 字节槽位，支持 64 位加减乘除/比较/位运算/移位，含符号区分）
- [x] `unsigned` / `signed` 限定符（64 位比较/右移/除法已区分有符号与无符号）
- [x] `float` / `double` / `long double`（存储、赋值、四则运算、比较、函数传参/返回、与整数互转）
- [x] `_Bool`（1 字节存储，赋值归一化为 0/1）
- [x] `const` 只读检查（赋值/自增自减/复合赋值均报错）
- [x] `volatile` / `restrict`（语法解析支持，当前不产生额外语义）
- [x] `struct`（成员访问、整体赋值、按值返回、指针返回）
- [x] `union`（成员共享存储）
- [x] `enum`（枚举常量）
- [x] `typedef`（类型别名，含 `typedef struct { ... } Name`、指针/数组/函数指针别名）
- [x] 指针、数组、多维数组（指针算术、`&`/`*`、下标、指针自增/自减、数组参数退化）
- [x] 函数指针（声明、`typedef`、间接调用、作为参数/返回值）
- [x] `void *`（通用指针、指针转换）
- [x] 显式类型转换 `(int)/(float)/(double)/(int*)` 等
- [x] `sizeof`（`sizeof(type)` 与 `sizeof(expr)`）
- [x] 字符串字面量 / `char *`（含 `char s[] = "..."` 初始化）
- [x] 数组初始化列表 `{ ... }`（含 `int a[] = {...}`、多维数组、全局数组初始化）

### 语句 / 语法

- [x] `switch` / `case` / `default`
- [x] `do ... while`
- [x] `goto` / 标号
- [x] 三目运算符 `?:`
- [x] 逗号表达式
- [x] 可变参数 `...` 与 `stdarg.h`（`va_list`/`va_start`/`va_arg`/`va_end`/`va_copy`）
- [x] `inline`（解析支持）
- [x] `static` / `extern`（文件作用域存储类；extern 声明不分配存储）
- [x] `static` 局部变量（静态存储期，DATA 段分配并只初始化一次）
- [x] `__interrupt__` ISR（`void f(void) __interrupt__`，返回使用 `IRET`）
- [ ] `_Static_assert`
- [ ] 匿名结构体 / 联合体
- [ ] 位域

### 表达式

- [x] 指针算术
- [x] 下标 `a[i]`
- [x] 成员访问 `.` / `->`
- [x] `&` 取地址 / `*` 解引用
- [x] 指针自增/自减
- [ ] `_Generic`

### 预处理 / 标准库

- [x] `#include`（支持 `-I`、`-ffreestanding`、`-fhosted`）
- [x] `#define` / 宏（对象宏/函数宏）
- [x] `#undef`
- [x] `#if` / `#ifdef` / `#ifndef` / `#elif` / `#else` / `#endif`（常量表达式与 `defined`）
- [x] `#error`
- [x] `#pragma`（忽略，不报错）
- [ ] 可变参数宏 `__VA_ARGS__`
- [ ] `switch` 每个 case 自动跳转到末尾，不支持 C 的 fallthrough
- [ ] 字符字面量（`'A'`）与八进制字面量暂不支持
- [ ] 完整 C 标准库（已提供 freestanding 常用头文件：`stdarg.h`、`stddef.h`、`stdbool.h`、`stdint.h`、`inttypes.h`、`io.h`、`math.h`、`string.h`）

### 代码生成 / 工具链

- [x] ELF 目标文件输出（`-m elf`）
- [x] 平坦二进制输出（`-m bin`）
- [x] 多文件编译（`dcc a.c b.c out.asm`，含自动查找并合并 lib 实现）
- [x] 内联汇编（`__asm__`）与寄存器直访（`__reg_*`）
- [x] 结构体按值返回（通过 `struct_ret` 缓冲）
- [ ] 优化
- [ ] 调试信息
- [ ] 结构体按值传参
