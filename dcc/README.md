# dcc — DOCTOR C 编译器（C99 实现）

`dcc` 是为 **DOCTOR ISA v3.3** 编写的 C 编译器。当前实现以标准 C99 重写，
取代了早期的 C++ 最小实现；它把 **C99 的一个可用子集** 编译为 **dasm 汇编文本**，
再经 `dasm` 汇编成二进制，由 `doctor-sim` 模拟器执行。

```
C 源码 (.c) → dcc → dasm 汇编 (.asm) → dasm → 二进制 (.code/.data) → doctor-sim
C 源码 (.c) → dcc -m elf → ELF32 可重定位 (.o) → dlinker → 二进制 (.code/.data)
```

当前版本只做**翻译**，不做优化。目标平台为 32 位 DOCTOR（小端、哈佛结构）。
与早期版本相比，本版新增/完善了：

- `long long`（8 字节 64 位整数）与完整 64 位算术、比较、移位、除法/取余
- `float` / `double` / `long double` 的存储、赋值、四则运算、比较、传参/返回和类型转换
- `switch` / `case` / `default`、`do ... while`、`goto` / 标号
- `#if` / `#elif` 常量表达式、`#error`、`#pragma` 忽略
- `static` 局部变量（静态存储期）、文件内 `static` 函数/变量、`extern`、`inline`
- 结构体整体赋值与按值返回；数组初始化列表（局部/全局、多维）
- `volatile` / `restrict` 语法支持；`_Bool` 归一化
- 可变参数 `...` 与 `stdarg.h`

## 构建

```sh
make            # 生成 ./dcc
make clean      # 清理构建产物
```

依赖：C99 编译器（gcc/clang）。不需要 C++17 工具链。

## 用法

```sh
./dcc [选项] <input.c> [more.c ...] [output]
```

- 单文件：`dcc myprog.c` → `myprog.asm`；或 `dcc myprog.c out.asm`。
- 也可用 `-o` 指定输出：`dcc -m elf -o myprog.o myprog.c`。
- `-m asm`（默认）：生成 DASM 汇编文本。
- `-m elf`：`dcc -m elf myprog.c` → `myprog.o`（ELF32 relocatable）；
  或 `dcc -m elf myprog.c out.o`。
- `-m bin`：调用 dasm 直接生成 `<name>_code.bin` 和 `<name>_data.bin`。
- `-I<dir>` / `-I <dir>`：添加头文件搜索目录。
- `-ffreestanding`：使用 `lib/freestanding/include` 作为默认库头文件目录。
- `-fhosted`（默认）：使用 `lib/hosted/include` 作为默认库头文件目录。
- 最后一个参数以 `.asm` / `.bin` / `.o` 结尾视为输出；也可用 `-o` 显式指定。
  否则默认 `<第一个输入去扩展名>.asm`（`-m elf` 时为 `.o`，`-m bin` 时为 `.bin`）。
- 成功打印 `dcc: N 个输入文件 → <output> (ok, format=...)`；任一阶段出错则输出错误并返回非零退出码。

### 多文件分离编译

```sh
./dcc main.c lib.c out.asm
./dcc -m elf main.c lib.c out.o
```

每个 `.c` 独立预处理后合并（extern 变量/函数原型去重），输出一个 `.asm` 或 ELF 目标文件。
声明放头文件，实现在 `.c`：

```c
/* mathlib.h */     extern int g_count;  int add(int a, int b);
/* lib_math.c */    #include "mathlib.h"  int g_count = 0;  int add(int a, int b){...}
/* main.c */        #include "mathlib.h"  int main(void){ return add(1,2); }
```

### 完整工具链示例

```sh
# 1. C → dasm 汇编（多文件）
dcc/dcc main.c lib.c myprog.asm

# 2a. 也可以让 dcc 直接生成 ELF 目标文件（等价于 dcc→dasm -m elf）
dcc/dcc -m elf main.c lib.c myprog.o

# 2b. dasm 汇编 → 二进制
dasm/dasm myprog.asm myprog.code myprog.data

# 3. 模拟器运行（SIGINT 2 秒后转储寄存器，查看 A 寄存器结果）
timeout -s INT 2 ./doctor-sim/build/bin/doctor-sim -f code myprog.code data myprog.data
```

dcc 不自动生成 `_start` 入口。默认入口符号为 `func_main`，由用户自行链接 CRT：

- `bootable_crt.asm`：设置 `S=0x300000` 后跳转 `func_main`
- `bin_crt.asm`：不设置 `S`，只跳转 `func_main`

`main` 的返回值最终出现在 **A 寄存器**。

## 测试

```sh
make test          # 编译全部 tests/*.c 并调用 dasm 汇编（冒烟测试）
make test-elf      # 等价于 sh run_elf_test.sh
sh run_elf_test.sh # -m elf 冒烟测试
```

`make test` 覆盖 24 个测试场景，包括：函数调用/多参数、char/short/long/long long、
signed/unsigned 类型、浮点与转换、`_Bool`、指针与数组、多维数组、字符串、结构体、
typedef/union/enum、函数指针、`void *`、可变参数、`switch`/`do`/`goto`、`static`/`extern`/
`inline`、`__interrupt__` ISR、内联汇编与寄存器直访、预处理（`#include`/`#define`/
`#if`/`#elif`/`#ifdef`/`#ifndef`）、分离编译和自动查找 lib 实现。

## 目录结构

```text
dcc/
├── Makefile            # 构建脚本（C99）
├── README.md           # 本文件
├── TODO.md             # 尚未实现/已知限制
├── run_elf_test.sh     # -m elf 冒烟测试
├── bootable_crt.asm    # 可启动 CRT：设置 S 后跳转 func_main
├── bin_crt.asm         # 平坦 CRT：不设置 S，只跳转 func_main
├── src/                # 编译器源码（C99）
│   ├── lexer.h/.c      # 词法分析
│   ├── preprocessor.h/.c  # 预处理器（#include / #define / #if 等）
│   ├── ast.h/.c        # 语法树与程序表示
│   ├── parser.h/.c     # 递归下降语法分析
│   ├── codegen.h/.c    # 代码生成（AST → dasm 文本）
│   └── main.c          # 命令行入口
├── lib/                # 自动查找实现与默认头文件
│   ├── freestanding/
│   │   ├── include/    # stdarg.h stddef.h stdbool.h stdint.h inttypes.h io.h math.h string.h
│   │   └── *.c         # io.c math.c string.c
│   └── hosted/
│       ├── include/    # stdarg.h autotest.h
│       └── autotest.c
├── docs/
│   ├── specification.md        # 语言子集规范
│   └── calling-convention.md   # 调用约定与代码生成规则
└── tests/              # 冒烟测试用例（.c）
    ├── hello.c
    ├── long64.c
    └── ...
```

## 支持的语言子集

完整细节见 `docs/specification.md`。要点：

- **类型**：`int`（4 字节）、`unsigned int`、`short`（2 字节）、
  `long` / `long long`（8 字节，64 位）、`char`（1 字节，裸 `char` 按无符号 0-255）、
  `signed char`、`unsigned char`、`_Bool`、`float`（4 字节 DFE）、
  `double` / `long double`（8 字节 DDE）、`void *`、指针、`struct`、`union`、
  `enum`、`typedef`、`const` / `volatile` / `restrict`
- **寄存器直访**：`__reg_A` / `__reg_B` / `__reg_C` / `__reg_D1` / `__reg_D2` /
  `__reg_X` / `__reg_I` / `__reg_S` / `__reg_R` / `__reg_F` / `__reg_T` 直接读写
  DOCTOR 寄存器（类型为 `unsigned int`，可作左值/右值；支持 `=`、复合赋值、
  `++`/`--`；右移为逻辑右移、除法/取余为无符号）。`__reg_E` 只能作为右值读取。
- **指针/数组**：`&` / `*`、指针算术 `p+n`/`p-n`（按所指类型缩放）、下标 `p[i]`、
  一维/多维数组、数组参数退化、指针自增/自减、指针比较、函数指针、`void *`
- **字符串**：字符串字面量可用于 `char *p = "..."`、`char s[] = "..."`、
  函数参数、下标访问
- **语句**：表达式、`return`、`if/else`、`while`、`for`、`do ... while`、
  `switch/case/default`、`break`、`continue`、`goto`、标号、块、变量声明、
  `__asm__` 内联汇编
- **表达式**：算术、位、移位、比较、逻辑、三元、逗号、赋值与复合赋值、
  自增自减、成员访问、`sizeof`、强制类型转换
- **函数**：返回值在 D1/A（浮点走 FP/DP 寄存器，long 走 A:D1，结构体通过
  `struct_ret` 返回）；参数经栈传递（见调用约定文档）；支持 `static` / `extern` /
  `inline`、函数原型、可变参数、`__interrupt__` ISR
- **预处理**：`#include`、`#define`（对象宏/函数宏）、`#undef`、`#error`、
  `#if` / `#elif` / `#else` / `#endif` / `#ifdef` / `#ifndef`（含 `defined` 与常量表达式）
- **多文件与自动 lib**：`dcc a.c b.c out.asm` 合并多个编译单元；`#include <...>` 后
  自动从 `lib/<mode>/<name>.c` 查找未实现的函数原型

## 已知限制

- 不支持位域、匿名结构体/联合体、`_Static_assert`、`_Generic`
- 结构体支持整体赋值和按值返回，但**不支持结构体按值传参**（请使用指针）
- `switch` 每个 case 自动跳转到末尾，不支持 C 的 fallthrough
- 字符字面量（`'A'`）与八进制字面量暂不支持
- 移位右操作数必须为编译期常量
- 可变参数宏（`__VA_ARGS__`）不支持；`#pragma` 被忽略
- 未定义函数/变量报错；不支持动态链接
- 当前只做“正确翻译”，不做优化，也没有调试信息

## 预处理器 `#include` / `#define`

```c
#include <io.h>             /* 默认查找: 当前 dcc/lib/<mode>/include */
#include "inc/local.h"      /* 先找当前文件目录，再回退库 include 目录 */

#define N 5                 /* 对象宏 */
#define SQUARE(x) ((x)*(x)) /* 函数宏 */
#define MAX(a,b) ((a)>(b)?(a):(b))

#if defined(FEATURE_ON) && N > 3
/* ... */
#elif N == 5
/* ... */
#else
/* ... */
#endif
```

- `#include <foo.h>` 默认在 `lib/freestanding/include`（`-ffreestanding`）或
  `lib/hosted/include`（`-fhosted`）查找；`#include "foo.h"` 先找当前文件所在目录，
  再回退库 include 目录。`-I` 指定的目录优先级最高。
- `#define` 支持对象宏与函数宏；宏体在使用处递归展开（防自递归）；
  字符串/字符字面量内不展开；实参先展开再替换。
- 条件编译支持 `#if` / `#elif` 常量表达式（含 `defined`）、`#ifdef` / `#ifndef`、
  `#else` / `#endif`。
- 被包含文件同样预处理；宏表跨文件共享，条件栈每文件独立。
- 未知 `#` 指令（如 `#pragma`）被忽略。

## 中断服务函数 `__interrupt__`

```c
void timer_isr(void) __interrupt__;   /* 原型声明为 ISR（须 void 返回、无参数） */
int ticks;

void timer_isr(void) {                /* 实现处为普通形式（属性从原型继承） */
    ticks = ticks + 1;
    return;                           /* 编译为 MOV S,F; POP F; IRET */
}
```

- 原型 `void foo(void) __interrupt__;` 标记 ISR；必须返回 `void` 且无参数。
- 实现处 `void foo(void) {...}` 不带修饰，合并时继承原型的 `__interrupt__` 属性。
- ISR 函数体 `return;` 与尾声均编译为 `MOV S, F; POP DWORD F; IRET`。

## 内联汇编 `__asm__`

```c
__asm__("MOV A, DWORD 42");
__asm__("LET B, DWORD 0x1234\n"
        "ADD DWORD A, B");
```

- 字符串内容为 **DASM 汇编文本**（可多行、多条指令、注释 `;`）。
- 相邻字符串字面量自动拼接。
- 无操作数约束；寄存器/标签由你自行管理（dcc 不感知 asm 内部行为）。
- 通过 A 寄存器与 C 代码交换数据。

## 与 ISA 同步

dcc 生成的汇编遵循 `manual.md` 的指令契约（大小写不敏感、显式尺寸、
NZ/REP 前缀等），并依赖 dasm 的全部特性（标签、表达式、`DB/DD/RESB` 等）。
修改 ISA 时请保持 `manual.md`、模拟器、dasm、dcc 四处同步。

## 已知问题

- dasm 的 `DB/DW/DD` 伪指令只接受单个数据值（`DB <addr>, <value>`）；
  dcc 生成的数据区字符串按每字节一行 `DB <addr>, 0xXX` 输出。
- dasm 预处理会在字符串内剥离 `;` 后的内容；dcc 生成的数据区字符串一律以
  十六进制字节输出以规避此限制。
- 生成的代码不包含 `_start`；默认入口符号为 `func_main`，由用户自行链接
  `bootable_crt.asm` 或 `bin_crt.asm`。
