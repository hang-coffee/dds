# dcc 语言子集规范 (v0.1)

本文档定义 `dcc` 支持的 C 语言子集。目标：把 ANSI C 的一个可用子集
翻译为 DOCTOR dasm 汇编。除本文档明示的差异外，语义与 ANSI C 一致。

## 1. 词法

### 注释

- `// 行注释`
- `/* 块注释 */`（不嵌套）

### 字面量

| 形式 | 例子 | 类型 | 说明 |
|---|---|---|---|
| 十进制 | `42` | int | |
| 十六进制 | `0x2A` / `0X2A` | int | |
| 八进制 | `052` | int | 前导 0 |
| 字符 | `'A'` | char | 转义：`\n \t \r \\ \' \" \0` |
| 字符串 | `"abc"` | char[] | 仅用于 char 数组初始化 |

### 标识符与关键字

标识符：`[A-Za-z_][A-Za-z0-9_]*`。

关键字：`int char void return if else while for break continue`。

运算符/分隔符：
`+ - * / % = == != < <= > >= && || ! & | ^ ~ << >> ++ -- += -= *= /= %=`
`&= |= ^= <<= >>= ( ) { } [ ] ; , ? :`

> 注意：`void` 仅用于函数返回类型或 `(void)` 空参数列表；不支持
> `void*` 等指针形式。

## 2. 类型

| 类型 | 大小 | 符号性 | 说明 |
|---|---|---|---|
| `int` | 4 字节 | 有符号（32 位补码） | |
| `unsigned int` / `unsigned` | 4 字节 | 无符号 | |
| `short` / `unsigned short` | 2 字节 | 有/无符号 | 读写按 WORD；有符号读时符号扩展 |
| `long` / `unsigned long` | 8 字节 | 有/无符号 | 64 位（A=低 32 位、D1=高 32 位）；不支持 `long long` |
| `char` | 1 字节 | **无符号 0-255** | 裸 `char` 固定为无符号（与 v0.1 一致） |
| `_Bool` | 1 字节 | 无符号 | C99 布尔；赋值/返回时任意非 0 值归一化为 1 |
| `float` | 4 字节 | — | 单精度浮点，使用 DFE 指令 |
| `double` / `long double` | 8 字节 | — | 双精度浮点，使用 DDE 指令 |
| `signed char` | 1 字节 | 有符号 | 读取时符号扩展（-128..127） |
| `unsigned char` | 1 字节 | 无符号 | |
| `struct S` / `union U` | 布局尺寸 | — | 成员顺序布局（union 取最大成员） |
| `enum E` | 4 字节 | 无符号 int | 常量可在编译期使用 |
| `void *` | 4 字节 | 无符号地址 | 通用指针；不能解引用，与其它指针互转需显式转换 |
| `int *` / `char *` 等 | 4 字节 | 无符号地址 | 指针；指向类型决定解引用尺寸与算术缩放 |
| `const T` | 同 T | — | 只读限定；赋值给 const 变量报错 |

- `float`/`double`/`long double` 已支持；`long` 为 64 位（8 字节）；**显式强制转换** `(type)expr` 支持（见 §2.2）。
- **long 值约定**：寄存器中 A=低 32 位、D1=高 32 位；内存小端序（低字在前）。
- **long 运算**：`+ - * / %`（有/无符号）、`<< >>`（右移按左操作数符号性：算术 `MSR`/
  逻辑 `SHR`）、`& | ^ ~`、比较（先高字后低字；两操作数均 unsigned 时高字用无符号
  比较，否则有符号；高字相等后低字恒无符号）、一元 `-`、逻辑真值（低|高非零）。
  `(long)x` 按**目标**符号性扩展 D1；`(int)long` 截断低 32 位。
- **long 函数**：long 参数占 8 字节栈槽位（低字先压），long 返回值留在 A:D1。
- **64 位除法**为移位减法（恢复余数法）；除数为 0 触发硬件 `#DIV`。
- 指针类型：`T *`（可多层 `T **`）。指针算术 `p+n` 按 `sizeof(*p)` 缩放
  （char 1、short 2、int 4、指针 4）。

### 2.0 用户定义类型：`struct` / `union` / `enum` / `typedef`

```c
typedef unsigned int u32;                 /* 类型别名 */

struct Point { int x; int y; };           /* 定义（不产生变量） */
struct Point p1;                          /* 引用使用 */
typedef struct Point Point;
Point p2;

typedef struct Rect {                      /* 定义 + 别名 */
    Point tl;                             /* 嵌套结构体成员 */
    Point br;
} Rect;

union Value {                             /* 联合体：成员共享存储 */
    int i;
    char bytes[4];
};

enum Color { RED, GREEN = 5, BLUE };      /* 枚举：0, 5, 6 */
enum Color c = BLUE;
```

- **struct**：成员按声明顺序连续布局（无对齐填充）；大小 = 成员尺寸之和。
- **union**：所有成员共享起点（offset 0）；大小 = 最大成员尺寸。
- 成员访问：`s.field`（结构体变量）、`p->field`（结构体指针）；
  数组成员可用下标 `s.arr[i]`；支持嵌套（`r.tl.x`）。
- 结构体变量可整体赋值（`r1 = r2`，块拷贝）；**按值传参/返回不支持**（用指针）。
- **enum**：常量默认从 0 递增；可显式赋值（`GREEN = 5`）或引用其它常量；
  枚举常量在编译期替换为整数值；枚举变量按 `int`（4 字节）处理。
- **typedef**：为内置类型、struct/union/enum、指针创建别名；`typedef` 名可
  用于变量/参数/成员声明；数组 typedef 不支持。
- 自引用结构体：`struct Node { int v; struct Node *next; };`（指针成员，
  恒 4 字节）。
- 作用域：类型定义在**整个编译单元**（含其后所有函数）可见；跨文件共享
  （多文件合并时类型环境全局共享）。

### 2.1 寄存器直访 `__reg_`

`__reg_A` / `__reg_B` / `__reg_C` / `__reg_D1` / `__reg_D2` / `__reg_X` /
`__reg_I` / `__reg_S` / `__reg_R` / `__reg_F` / `__reg_T` 直接读写对应
DOCTOR 寄存器；`__reg_E` 只能作为右值读取：

```c
__reg_X = 42;        /* MOV X, A */
__reg_X++;           /* 寄存器自增 */
__reg_I = __reg_X;   /* MOV I, X */
unsigned int v = __reg_X;   /* MOV A, X 后存变量 */
```

- 类型恒为 **`unsigned int`**（4 字节位模式），可作左值/右值。
- 支持 `=`、复合赋值（`+= -= *= /= %= &= |= ^= <<= >>=`）、`++`/`--`。
- 无符号语义：`>>` 为逻辑右移（`SHR`）、`/` `%` 为无符号除法/取余、
  与其它 unsigned 比较用无符号跳转。
- 支持 `A/B/C/D1/D2/X/I/S/R/F/T` 作左值/右值；`E` 只能作右值。
- 寄存器无内存地址（`&__reg_A` 报错）。
- 内部临时寄存器已避开 `X`/`I`（比较基准用 `T`、有符号除法符号用 `R`+栈）；
  但 `C` 是 `CMP` 目标、`D1/D2` 是乘除结果——`__reg_C`/`__reg_D1`/
  `__reg_D2` 的取值依赖表达式求值顺序。

### 2.2 强制类型转换 `(type)expr`

```c
char c = 'k';
int s = (int)c;          /* char → int：值不变 */
char ch = (char)300;     /* int → char：截断低 8 位 = 44 */
unsigned int u = (unsigned int)(-1);   /* 重解释为 0xFFFFFFFF */
int *ip = (int *)0x2000; /* 整数 → 指针：值不变 */
char *cp = (char *)ip;   /* 指针互转：值不变 */
```

- `(char)x` / `(unsigned char)x`：截断低 8 位（`SHL 24; SHR 24`）。
- `(int)x` / `(unsigned)x` / 指针转换：仅重解释位模式，无运算。
- 转换操作数须为表达式；不支持结构体（不存在）。

### 类型影响代码生成

| 场景 | 有符号 | 无符号 |
|---|---|---|
| 关系比较 `< <= > >=` | `JL/JNG/JG/JNL` | `JB/JNB/JA/JNA` |
| 右移 `>>` | 算术右移 `MSR` | 逻辑右移 `SHR` |
| 除法/取余 `/ %` | 内联符号处理序列 | 直接 `DIV` |
| `char` 读取 | 符号扩展（`SHL 24; MSR 24`） | 直接 `LR BYTE` |

> 混合符号比较：仅当**两个操作数都是 unsigned** 时用无符号比较；
> 否则按有符号。指针比较按地址（无符号）。

## 3. 声明

### 全局变量

```c
int g;                 /* 零初始化（RESB） */
int g2 = 42;           /* DD 立即数 */
unsigned int gu = 7;   /* DD */
char gc = 'A';         /* DB */
signed char gsc = -5;  /* DB 0xFB */
int arr[8];            /* RESB 8*4 */
char msg[8] = "hi";    /* 逐字节 DB + NUL，不足部分 RESB */
char *gp = "abc";      /* DD 指向字符串常量（常量紧随其后） */
int *gip = 0;          /* 零指针 */
extern int shared;     /* 声明（不分配存储；由其它编译单元定义） */
extern int garr[4];    /* extern 数组声明 */
```

- 全局初始化器**必须是编译期常量**（数字/字符/字符串字面量）。
- 全局数组不支持 `{}` 列表初始化；`char` 数组可用字符串初始化。
- `char *g = "..."`：生成字符串常量（紧跟变量之后）并用 `DD` 指向。
- **`extern` 声明**：不分配存储，仅登记符号（引用解析到定义者的 `var_<name>`）；
  不带初始化器；若多文件合并后无定义者 → 报错「extern 变量未定义」。

### 局部变量

```c
int a, b;              /* 逗号分隔多条声明 */
char c;
int *p;                /* 指针 */
char *s = "abc";       /* 指针指向字符串常量 */
int arr[4];
char buf[6] = "abc";   /* 字符串拷贝到局部数组（生成拷贝循环） */
int x = 10;            /* 标量初始化（生成赋值代码） */
```

- 局部变量分配在栈帧（`F+4` 起），声明顺序决定偏移。
- 局部数组的字符串初始化生成一段拷贝循环（`LOD/ST`）。
- 局部指针的字符串初始化生成地址赋值（`LET A, <str>; ST`）。
- 作用域：局部变量在函数内可见；块 `{}` 不引入新作用域
  （v0.2 简化：整个函数共享一个符号表，同名变量重复声明报错）。

## 4. 语句

| 语句 | 形式 | 说明 |
|---|---|---|
| 表达式 | `expr;` | 丢弃结果 |
| 返回 | `return expr;` / `return;` | `return;` 返回 0 |
| 分支 | `if (cond) stmt [else stmt]` | cond 非零为真 |
| 循环 | `while (cond) stmt` | |
| 循环 | `for (init; cond; inc) stmt` | 三部分均可省略 |
| 跳转 | `break;` / `continue;` | 必须在循环内（for/while） |
| 块 | `{ stmt* }` | |
| 声明 | `type name [= init] [, ...];` | 见 §3 |
| 内联汇编 | `__asm__("dasmasm");` | 见 §4.1 |

### 4.1 内联汇编 `__asm__`

```c
__asm__("MOV A, DWORD 42");
__asm__("LET B, DWORD 0x1234\n"
        "ADD DWORD A, B");
```

- 字符串内容为 **DASM 汇编文本**，可多行、多条指令、`;` 注释。
- 相邻字符串字面量自动拼接（GNU 语义）。
- **无操作数约束**：不声明输入/输出，寄存器由程序员自行管理。
  `__asm__` 修改的寄存器会被 dcc 视为已改变——若需保留，须在
  asm 内自行 `PUSH`/`POP` 或用其它寄存器。
- 生成的汇编中，asm 文本按行原样嵌入 TEXT 段当前位置。

## 5. 表达式

优先级从高到低（与 ANSI C 一致）：

1. 后缀：`a[i]`、`f(args)`、`x++`、`x--`
2. 一元：`-x`（算术取反 MNE）、`~x`（按位取反 NEG）、`!x`（逻辑非）、
   `&x`（取地址）、`*p`（解引用）
3. 强制转换：`(type)expr`（优先级高于乘除）
3. 乘除模：`* / %`
4. 加减：`+ -`（指针 ± 整数按 `sizeof(*p)` 缩放）
5. 移位：`<< >>`（**右操作数必须是编译期常量**；`>>` 有符号用 `MSR`、
   无符号用 `SHR`）
6. 关系：`< <= > >=`（两操作数均 unsigned 用无符号跳转）
7. 相等：`== !=`
8. 位与：`&`
9. 位异或：`^`
10. 位或：`|`
11. 逻辑与：`&&`（短路）
12. 逻辑或：`||`（短路）
13. 条件：`c ? a : b`
14. 赋值：`= += -= *= /= %= &= |= ^= <<= >>=`（右结合；指针 `+=`/`-=` 缩放）

### 语义要点

- 除法/取余：`/` 商、`%` 余数；除数为 0 → 触发 #DIV 异常（模拟器行为）。
  - unsigned：直接 `DIV`（D2=商, D1=余）
  - signed：内联符号处理序列（取绝对值→除→按符号修正）
- 移位：左移 `<<` 用 `SHL`；右移 `>>` 有符号用 `MSR`（算术）、
  无符号用 `SHR`（逻辑）。
- 比较：有符号 `JL/JNG/JG/JNL`；无符号（两操作数均 unsigned）
  `JB/JNB/JA/JNA`；`== !=` 用 `JZ/JNZ`。
- `&&`/`||` 短路求值；`!` 把非零压成 1。
- 自增自减：前/后缀都支持；表达式值分别为"新值"/"旧值"。
  指针 `++`/`--` 步进 `sizeof(*p)`。
- 取地址 `&x`：x 须为左值（变量、数组元素、`*p`）。
- 解引用 `*p`：p 须为指针；`signed char *` 解引用会符号扩展。

## 6. 函数

```c
int add(int a, int b) { return a + b; }
int main(void) { return add(2, 3); }
int sum(int *p, int n) { ... }       /* 指针参数：传地址 */
char *greet(void) { return "hi"; }   /* 返回 char* */
```

- 返回值类型：`int` / `char` / `_Bool` / `void` / 指针。
- `void` 函数仍可 `return;`（生成与有返回值相同的尾声，忽略即可）。
- 参数：`int`/`char`/指针，按值传递，经栈传递（调用约定见
  `docs/calling-convention.md`）。
- dcc 不再生成 `_start`；默认入口符号为 `func_main`，用户自行链接 `bootable_crt.asm` / `bin_crt.asm`。
- 函数指针：支持 `ret (*name)(params)`、`typedef` 函数指针、函数名/`&func` 作为地址、
  通过函数指针间接调用（`fp(args)` 与 `(*fp)(args)`）；不支持函数指针数组、返回函数指针的复杂声明符
  （可用 typedef 间接表达）。
- 可变参数：支持 `...` 与 `<stdarg.h>`（`va_list`/`va_start`/`va_arg`/`va_end`/`va_copy`）；
  当前可变参数按 4 字节槽位传递，适合 `int`/指针/`char*` 等。
- 不支持：递归深度无限制（受栈空间约束）。

### 6.1 函数原型与分离编译

```c
/* mathlib.h（声明，可被多个 .c include） */
extern int g_count;
int add(int a, int b);
int mul(int a, int b);

/* lib_math.c（实现） */
#include "mathlib.h"
int g_count = 0;
int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }

/* main.c */
#include "mathlib.h"
int main(void) { return add(g_count, 2); }
```

- **函数原型**：`int foo(int a, int b);`（以 `;` 结尾、无函数体）——只登记
  函数表（供调用解析），**不生成代码**。
- **extern 函数声明**：`extern int foo(int);` 等价于原型。
- **多文件编译**：`dcc main.c lib.c out.asm` 把多个 `.c` 独立预处理/解析后
  合并：
  - 原型与定义同名 → 保留定义（原型占位被替换）
  - 同名函数**两个定义** → 报错「函数重复定义」
  - extern 变量须有定义者，否则报错「extern 变量未定义」
- **自动查找 lib**：`#include <foo.h>` 后，若头文件中的函数原型在已编译
  文件中未被实现，编译器自动从 **`项目根/dcc/lib/foo.c`** 加载实现：
  ```c
  /* io.h 只有原型 */        extern int inb(uint16_t port);
  /* 使用处 */               #include <io.h>  int main(void){ return inb(0x60); }
  /* 自动编译 lib/io.c（含 inb 实现）并合并 */
  ```
  - lib 目录与 include 目录同级（`dcc/lib`）；环境变量 `DCC_INCLUDE`
    覆盖 include 目录时 lib 目录随之推导。
  - 用户显式提供的同名实现优先（不重复加载）；lib 文件之间互相 include
    形成循环时安全终止。
- 未在附加源文件中定义的函数 → 生成对 `func_<name>` 的引用，
  dasm 报未定义标号（链接期错误）。

## 7. 预处理器

在词法分析前运行，支持以下指令（其余 `#` 指令报错）：

| 指令 | 说明 |
|---|---|
| `#include <foo.h>` | 在 **`项目根/dcc/include`** 查找（环境变量 `DCC_INCLUDE` 可覆盖） |
| `#include "foo.h"` | 先找当前文件所在目录，再回退 include 目录 |
| `#define NAME tokens` | 对象宏 |
| `#define NAME(a, b) tokens` | 函数宏（参数在调用处展开后替换） |
| `#undef NAME` | 取消宏定义 |
| `#ifdef NAME` / `#ifndef NAME` | 条件编译（含 include guard） |
| `#else` / `#endif` | 条件编译分支/结束 |
| `#pragma ...` | 忽略 |

- 宏展开为**文本级 token 替换**：对象宏体在使用处递归展开（防自递归，
  深度内可嵌套其它宏）；函数宏实参先展开再替换；字符串/字符字面量内
  不展开。
- 注释（`//` 与 `/* */`）在预处理阶段剥离（保留换行，字符串内不剥离）。
- 被包含文件同样预处理；**宏表跨文件共享**（头文件的 `#define` 影响
  包含者后续代码），**条件栈每文件独立**（`#endif` 不跨文件配对）。
- 允许重复包含（标准语义；无 include guard 的头文件重复包含会导致
  重定义报错——用 `#ifndef` guard 保护）。
- 限制：不支持 `#if`/`#elif` 表达式、`#error`、可变参数宏、反斜杠
  续行跨行宏。

## 8. 编译限制与错误

- 数组长度必须是正整数字面量（`int a[x]` 不支持）。
- 字符串不能赋给非 `char` 数组；字符串表达式类型为 `char*`。
- `a[i]` 中 `a` 可为数组名或指针。
- 移位右操作数非常量 → 报错。
- 全局初始化器非常量 → 报错。
- 逗号分隔的多变量声明支持（`int a, b, c;` / `int *p, *q;`）。
- 未定义变量/函数 → 报错。
- `*` 解引用非指针 → 报错；`&` 操作数非左值 → 报错。
- `__asm__` 内无操作数约束；不支持 GNU 的 `"r"` 等约束语法。
- 预处理错误（找不到 include、不支持指令、宏参数不匹配、条件栈不闭合）
  → 报错并带 `文件:行` 定位。

## 9. 代码生成说明

- 输出为 dasm 汇编文本：`SECTION DATA`（全局变量）+ `SECTION TEXT`
  （仅各函数 `func_<name>`，不包含入口）。
- 全局变量标号 `var_<name>`，函数标号 `func_<name>`，避免与 dasm
  指令关键字冲突。
- 字符串常量按**每字节一行** `DB <addr>, 0xXX` 输出（dasm 的
  `DB` 只接受单值）；`char *g = "..."` 用 `DD <addr>, <str>` 指向。
- 入口由用户链接的外部 CRT 提供：
  - `dcc/bootable_crt.asm`：设置 `S=0x300000` 后跳转 `func_main`
  - `dcc/bin_crt.asm`：不设置 `S`，只跳转 `func_main`

## 10. 与 ANSI C 的主要差异（速查）

| ANSI C | dcc v0.2 |
|---|---|
| `char` 符号性实现定义 | 裸 `char` 恒为无符号（0-255）；`signed char` 有符号 |
| `short`/`long`/`unsigned` 变体 | 支持 `short`/`int` + `signed`/`unsigned`；`long` 为 64 位（不支持 `long long`） |
| `struct`/`union`/`enum`/`typedef` | 支持（含嵌套/指针/整体赋值；无位域） |
| 预处理器 | `#include`/`#define`/`#ifdef`/`#ifndef`/`#else`/`#endif`（无 `#if` 表达式） |
| `{}` 数组初始化列表 | 不支持 |
| 移位右操作数任意 | 必须为编译期常量 |
| `>>` 对有符号右移 | 算术右移（MSR）；无符号逻辑右移（SHR） |
| 类型转换 `(t)x` | 支持（char 截断；int/unsigned/指针重解释） |
| 寄存器访问 | `__reg_A` 等扩展（非 ANSI） |
| 指针与整数隐式互转 | 不检查（v0.2 无转换检查） |
| `extern`/函数原型 | 支持（多文件分离编译，`dcc a.c b.c out.asm`） |
| GNU 内联汇编 | 支持 `__asm__`（无操作数约束） |
| `switch`/`do-while`/`goto` | 不支持 |
| 多字节/宽字符 | 不支持 |
