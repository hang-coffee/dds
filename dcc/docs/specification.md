# dcc 语言子集规范（C99 重写版）

本文档定义 `dcc` 支持的 C 语言子集。目标：把 C99 的一个可用子集翻译为
DOCTOR dasm 汇编。除本文档明示的差异外，语义与 C99 一致。

## 1. 词法

### 注释

- `// 行注释`
- `/* 块注释 */`（不嵌套）

### 字面量

| 形式 | 例子 | 类型 | 说明 |
|---|---|---|---|
| 十进制 | `42` | int | 可用 `u/U`、`l/L`、`ll/LL` 后缀 |
| 十六进制 | `0x2A` / `0X2A` | int | 同上 |
| 八进制 | `052` | int | 前导 0 表示八进制 |
| 字符 | `'A'` / `'\n'` | int | 字符字面量；支持常见转义 |
| 浮点 | `1.5` / `1.5f` / `2e3` / `1.5L` | double / float / long double | 带 `f/F` 后缀为 `float`；带 `l/L` 后缀为 `long double` |
| 字符串 | `"abc"` | char[] 字面量 | 表达式上下文中退化为 `char *`；用于 `char s[]` 初始化 |

### 标识符与关键字

标识符：`[A-Za-z_][A-Za-z0-9_]*`。

关键字：

```
int char void short long long unsigned signed const volatile restrict
float double _Bool struct union enum typedef static extern inline __interrupt__
return if else while for do goto break continue switch case default sizeof
```

运算符/分隔符：

```
+ - * / % = == != < <= > >= && || ! & | ^ ~ << >> ++ -- += -= *= /= %=
&= |= ^= <<= >>= -> ... ( ) { } [ ] ; , ? : . *
```

`__asm__` 与 `asm` 作为内联汇编关键字（词法上按标识符处理，由解析器识别）。
`__reg_A` 等是寄存器直访扩展。

## 2. 类型

| 类型 | 大小 | 符号性 | 说明 |
|---|---|---|---|
| `int` | 4 字节 | 有符号（32 位补码） | |
| `unsigned int` / `unsigned` | 4 字节 | 无符号 | |
| `short` / `unsigned short` | 2 字节 | 有/无符号 | 有符号读取时符号扩展 |
| `long` / `long long` | 8 字节 | 有/无符号 | 64 位；`long` 与 `long long` 同布局 |
| `char` | 1 字节 | **无符号 0-255** | 裸 `char` 固定为无符号 |
| `signed char` | 1 字节 | 有符号 | 读取时符号扩展（-128..127） |
| `unsigned char` | 1 字节 | 无符号 | |
| `_Bool` | 1 字节 | 无符号 | C99 布尔；赋值/返回时任意非 0 归一化为 1 |
| `float` | 4 字节 | — | 单精度浮点，DFE |
| `double` | 8 字节 | — | 双精度浮点，DDE |
| `long double` | 10 字节 | — | 80 位扩展精度浮点，DXE |
| `void *` | 4 字节 | 无符号地址 | 通用指针；与其它指针互转需显式转换；解引用按 DWORD 访问 |
| `T *` | 4 字节 | 无符号地址 | 指针；指向类型决定解引用尺寸与算术缩放 |
| `struct S` / `union U` | 布局尺寸 | — | 成员顺序布局（union 取最大成员） |
| `enum E` | 4 字节 | 无符号 | 枚举变量按 4 字节处理 |
| `const T` | 同 T | — | 只读限定；赋值/自增自减/复合赋值报错 |
| `volatile T` / `restrict T` | 同 T | — | 语法支持，当前不产生额外语义 |

- **64 位整数（`long`/`long long`）值约定**：寄存器中 A=低 32 位、D1=高 32 位；
  内存小端序（低字在前）。
- 支持 64 位：`+ - * / %`（有/无符号）、`<< >>`（右移按符号性：算术 `MSR`/
  逻辑 `SHR`）、`& | ^ ~`、比较、一元负号、复合赋值、`++`/`--`、函数参数与返回值、
  数组/指针/结构体成员、全局变量、逻辑真值（低|高非零）。
- 64 位除法为移位减法（恢复余数法）；除数为 0 触发硬件 `#DIV`。
- 指针类型：`T *`（可多层 `T **`）。指针算术 `p+n` 按 `sizeof(*p)` 缩放
  （char 1、short 2、int 4、指针 4、结构体按实际尺寸）。

### 2.0 用户定义类型：`struct` / `union` / `enum` / `typedef`

```c
typedef unsigned int u32;

struct Point { int x; int y; };
struct Point p1;
typedef struct Point Point;
Point p2;

typedef struct Rect {
    Point tl;
    Point br;
} Rect;

union Value {
    int i;
    char bytes[4];
};

enum Color { RED, GREEN = 5, BLUE };
enum Color c = BLUE;
```

- **struct**：成员按声明顺序连续布局（无对齐填充）；大小 = 成员尺寸之和。
- **union**：所有成员共享起点（offset 0）；大小 = 最大成员尺寸。
- 成员访问：`s.field`、`p->field`；支持嵌套、数组成员下标。
- 结构体变量可整体赋值（块拷贝）；支持**按值返回**（经 `struct_ret` 缓冲）；
  支持结构体按值传参（按字节拷贝到栈上）。
- **enum**：常量默认从 0 递增；可显式赋值或引用其它常量；枚举变量按 4 字节处理。
- **typedef**：可为内置类型、struct/union/enum、指针、数组、函数指针创建别名。
- 自引用结构体：`struct Node { int v; struct Node *next; };`。
- 支持匿名 struct/union 成员：内部成员直接并入父结构体/联合体，
  可通过父对象直接访问，例如 `o.i`、`o.a`。
- 支持位域：`unsigned int a : 3;`、`int b : 4;`，按 4 字节容器打包。
- 类型定义在**整个编译单元**可见；多文件合并时类型环境全局共享。
- 支持匿名 struct/union 成员（内部成员直接并入父类型）；支持位域。

### 2.1 寄存器直访 `__reg_`

`__reg_A` / `__reg_B` / `__reg_C` / `__reg_D1` / `__reg_D2` / `__reg_X` /
`__reg_I` / `__reg_S` / `__reg_R` / `__reg_F` / `__reg_T` 直接读写对应
DOCTOR 寄存器；`__reg_E` 只能作为右值读取。

```c
__reg_X = 42;
__reg_X++;
__reg_I = __reg_X;
unsigned int v = __reg_X;
```

- 类型恒为 `unsigned int`（4 字节位模式），可作左值/右值。
- 支持 `=`、复合赋值、`++`/`--`。
- 无符号语义：`>>` 为逻辑右移、`/` `%` 为无符号除法/取余。
- 寄存器无内存地址（`&__reg_A` 报错）。
- 内部临时寄存器已避开 `X`/`I`；但 `C` 是 `CMP` 目标、`D1/D2` 是乘除结果，
  `__reg_C`/`__reg_D1`/`__reg_D2` 的取值依赖表达式求值顺序。

### 2.2 强制类型转换 `(type)expr`

```c
char c = 107;   /* 'k' */
int s = (int)c;
char ch = (char)300;                 /* 截断低 8 位 = 44 */
unsigned int u = (unsigned int)(-1);
int *ip = (int *)0x2000;
char *cp = (char *)ip;
```

- `(char)x` / `(unsigned char)x`：截断低 8 位。
- `(short)` / `(unsigned short)`：截断低 16 位。
- `(int)x` / `(unsigned)x` / 指针转换：重解释位模式。
- `(float)` / `(double)` / `(long double)`：使用 `I2F`/`I2D`/`F2D`/`D2F`/`I2E`/`F2E`/`D2E`/`E2F`/`E2D` 等指令转换。
- 转换操作数须为表达式；不支持结构体转换。

### 类型影响代码生成

| 场景 | 有符号 | 无符号 |
|---|---|---|
| 关系比较 `< <= > >=` | `JL/JNG/JG/JNL` | `JB/JNB/JA/JNA` |
| 右移 `>>` | 算术右移 `MSR` | 逻辑右移 `SHR` |
| 除法/取余 `/ %` | 内联符号处理序列 | 直接 `DIV` |
| `char` 读取 | 符号扩展 | 直接 `LR BYTE` |

> 混合符号比较：仅当两个操作数都是 unsigned 时用无符号比较；否则按有符号。
> 指针比较按地址（无符号）。

## 3. 声明

### 全局变量

```c
int g;                 /* 零初始化（RESB） */
int g2 = 42;           /* DD 立即数 */
unsigned int gu = 7;
char gc = 65;   /* 'A' */
signed char gsc = -5;
int arr[8];            /* RESB 8*4 */
int list[3] = {1, 2, 3};
char msg[8] = "hi";
char *gp = "abc";
int *gip = 0;
extern int shared;
extern int garr[4];
```

- 全局初始化器必须是编译期常量（数字/字符/字符串字面量）。
- 全局数组支持 `{}` 列表初始化；`char` 数组可用字符串初始化。
- `extern` 声明不分配存储；若多文件合并后无定义者，报错「extern 变量未定义」。

### 局部变量

```c
int a, b;
char c;
int *p;
char *s = "abc";
int arr[4];
char buf[6] = "abc";
int x = 10;
```

- 局部变量分配在栈帧（`F+4` 起），声明顺序决定偏移。
- 局部数组的字符串初始化生成一段拷贝循环。
- 局部指针的字符串初始化生成地址赋值。
- `static` 局部变量在 DATA 段分配并只初始化一次。
- 块 `{}` 目前不引入独立作用域；整个函数共享一个符号表（同名重复声明的遮蔽
  语义未严格实现）。

## 4. 语句

| 语句 | 形式 | 说明 |
|---|---|---|
| 表达式 | `expr;` | 丢弃结果 |
| 返回 | `return expr;` / `return;` | `return;` 返回 0 |
| 分支 | `if (cond) stmt [else stmt]` | cond 非零为真 |
| 循环 | `while (cond) stmt` | |
| 循环 | `do stmt while (cond);` | |
| 循环 | `for (init; cond; inc) stmt` | 三部分均可省略 |
| 跳转 | `break;` / `continue;` | 必须在循环内 |
| 跳转 | `goto label;` | 标号：`label:` |
| 分支 | `switch (expr) { case ...: ... default: ... }` | 支持 C 的 case 穿透（fallthrough）；`break` 跳出 switch |
| 块 | `{ stmt* }` | |
| 声明 | `type name [= init] [, ...];` | 见 §3 |
| 内联汇编 | `__asm__("dasmasm");` | 见 §4.1 |

### 4.1 内联汇编 `__asm__`

```c
__asm__("MOV A, DWORD 42");
__asm__("LET B, DWORD 0x1234\n"
        "ADD DWORD A, B");
```

- 字符串内容为 DASM 汇编文本，可多行、多条指令、`;` 注释。
- 相邻字符串字面量自动拼接。
- 无操作数约束；寄存器由程序员自行管理。
- 生成的汇编中，asm 文本按行原样嵌入 TEXT 段当前位置。

## 5. 表达式

优先级从高到低（与 C 一致）：

1. 后缀：`a[i]`、`f(args)`、`x++`、`x--`
2. 一元：`-x`、`~x`、`!x`、`&x`、`*p`
3. 强制转换：`(type)expr`
4. 乘除模：`* / %`
5. 加减：`+ -`（指针 ± 整数按 `sizeof(*p)` 缩放）
6. 移位：`<< >>`（右操作数必须是编译期常量）
7. 关系：`< <= > >=`
8. 相等：`== !=`
9. 位与：`&`
10. 位异或：`^`
11. 位或：`|`
12. 逻辑与：`&&`（短路）
13. 逻辑或：`||`（短路）
14. 条件：`c ? a : b`
15. 赋值：`= += -= *= /= %= &= |= ^= <<= >>=`（右结合）

### 语义要点

- 除法/取余：除数为 0 → 触发 #DIV 异常。
- 移位：左移 `<<` 用 `SHL`；右移 `>>` 有符号用 `MSR`，无符号用 `SHR`。
- 比较：有符号 `JL/JNG/JG/JNL`；无符号（两操作数均 unsigned）`JB/JNB/JA/JNA`；
  `== !=` 用 `JZ/JNZ`。
- `&&`/`||` 短路求值；`!` 把非零压成 1。
- 自增自减：前/后缀都支持；指针步进 `sizeof(*p)`。
- 取地址 `&x`：x 须为左值。
- 解引用 `*p`：p 须为指针；signed char/short 解引用会符号扩展。
- `sizeof`：支持 `sizeof(type)` 与 `sizeof expr`；数组取总字节，字符串字面量取
  `strlen + 1`。

## 6. 函数

```c
int add(int a, int b) { return a + b; }
int main(void) { return add(2, 3); }
int sum(int *p, int n) { ... }
char *greet(void) { return "hi"; }
struct Point make_point(void) { ... return p; }
```

- 返回值类型：`int` / `char` / `short` / `long` / `long long` / `_Bool` /
  `float` / `double` / `long double` / 指针 / 结构体 / `void`。
- 普通标量返回值经 D1 通道；64 位经 A:D1；浮点经 FP/DP；结构体经 `struct_ret`。
- 标量/指针/浮点/64 位参数按值传递，经栈传递（调用约定见
  `docs/calling-convention.md`）；结构体按值传参已支持。
- 函数指针：支持声明、`typedef`、间接调用（`fp(args)` 与 `(*fp)(args)`）、
  作为参数/返回值。
- 可变参数：支持 `...` 与 `stdarg.h`；可变参数槽位统一按 8 字节传递。
- `static` 函数/变量具有内部链接；`extern` 函数声明等价于原型；`inline` 解析支持。
- 递归受栈空间限制。

### 6.1 函数原型与分离编译

```c
/* mathlib.h */
extern int g_count;
int add(int a, int b);

/* lib_math.c */
#include "mathlib.h"
int g_count = 0;
int add(int a, int b) { return a + b; }

/* main.c */
#include "mathlib.h"
int main(void) { return add(g_count, 2); }
```

- 函数原型以 `;` 结尾，只登记函数表，不生成代码。
- 多文件编译：`dcc main.c lib.c out.asm` 独立预处理/解析后合并。
  - 原型与定义同名 → 保留定义；
  - 同名函数两个定义 → 报错；
  - extern 变量无定义者 → 报错。
- 自动查找 lib：`#include <foo.h>` 后，若头文件中的函数原型在已编译文件中未被
  实现，编译器自动从 `dcc/lib/<mode>/foo.c`（或同名模块实现）加载实现。

## 7. 预处理器

在词法分析前运行，支持以下指令（其余 `#` 指令忽略）：

| 指令 | 说明 |
|---|---|
| `#include <foo.h>` | 在 `lib/<mode>/include` 查找 |
| `#include "foo.h"` | 先找当前文件所在目录，再回退库 include 目录 |
| `#define NAME tokens` | 对象宏 |
| `#define NAME(a, b) tokens` | 函数宏 |
| `#undef NAME` | 取消宏定义 |
| `#if expr` / `#elif expr` | 常量表达式（支持 `defined`） |
| `#ifdef NAME` / `#ifndef NAME` | 条件编译 |
| `#else` / `#endif` | 条件编译分支/结束 |
| `#error msg` | 报错 |
| `#pragma ...` | 忽略 |

- 宏展开为文本级 token 替换；函数宏实参先展开再替换；字符串/字符字面量内不展开。
- 注释在预处理阶段剥离（保留换行）。
- 被包含文件同样预处理；宏表跨文件共享，条件栈每文件独立。
- 限制：反斜杠续行跨行宏未完整支持；可变参数宏 `__VA_ARGS__` 已支持。

## 8. 编译限制与错误

- 数组长度必须是正整数字面量。
- 移位右操作数非常量 → 报错。
- 全局初始化器非常量 → 报错。
- 未定义变量/函数 → 报错。
- `*` 解引用非指针 → 报错；`&` 操作数非左值 → 报错。
- 结构体按值传参已支持；位域已支持；匿名 struct/union 已支持。
- `_Static_assert`、`_Generic` 不支持。
- 预处理错误（找不到 include、`#error`、宏参数不匹配、条件栈不闭合）→ 报错并带
  `文件:行` 定位。

## 9. 代码生成说明

- 输出为 dasm 汇编文本：`SECTION DATA`（全局变量/字符串/static 局部/struct_ret）+
  `SECTION TEXT`（仅各函数 `func_<name>`，不包含入口）。
- 全局变量标号 `var_<name>`，函数标号 `func_<name>`。
- 字符串常量按每字节一行 `DB <addr>, 0xXX` 输出；`char *g = "..."` 用
  `DD <addr>, <str>` 指向。
- 入口由用户链接外部 CRT：
  - `bootable_crt.asm`：设置 `S=0x300000` 后跳转 `func_main`
  - `bin_crt.asm`：不设置 `S`，只跳转 `func_main`

## 10. 与 C99 的主要差异（速查）

| C99 | dcc |
|---|---|
| `char` 符号性实现定义 | 裸 `char` 恒为无符号；`signed char` 有符号 |
| `long` / `long long` | 均按 8 字节 64 位整数实现 |
| `struct` / `union` | 支持；整体赋值/按值返回/按值传参；支持位域与匿名成员 |
| 结构体传参 | 不支持按值传参，需用指针 |
| 预处理 | 支持 `#include`/`#define`/`#if`/`#elif`/`#ifdef`/`#error`/可变参数宏 `__VA_ARGS__` |
| `#pragma` | 忽略 |
| 移位右操作数 | 必须为编译期常量 |
| `_Static_assert` / `_Generic` | 不支持 |
| 寄存器访问 | `__reg_A` 等扩展（非标准 C） |
| 内联汇编 | 支持 `__asm__`（无操作数约束） |
| 优化/调试信息 | 未实现 |
