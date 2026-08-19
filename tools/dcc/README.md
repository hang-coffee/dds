# dcc - DOCTOR C 编译器

`dcc` 是为 **DOCTOR ISA v3.3** 编写的 C 编译器（v0.2），把 **ANSI C 的可用于集** 编译为 **dasm 汇编文本**，再经 `tools/dasm` 汇编成二进制，由 `doctor_sim` 模拟器执行。

```
C 源码 (.c) → dcc → dasm 汇编 (.asm) → dasm → 二进制 (.code/.data) → doctor_sim
```

当前版本只做**翻译**，不做任何优化。目标平台为 32 位 DOCTOR（小端、哈佛结构）。
v0.4 新增：`__interrupt__`（ISR，return→IRET）、`sizeof`、`void*`、`long`、
`const`、确认宏函数。v0.3 已有：`short`、`typedef`/`struct`/`union`/`enum`、
成员访问。v0.2 已有：`signed`/`unsigned`、指针、字符串、内联汇编
`__asm__`、寄存器直访 `__reg_`、类型转换、预处理器、分离编译
（含自动查找 lib）。

## 构建

```sh
make            # 生成 ./dcc
make clean      # 清理构建产物
```

依赖：C++17 编译器（g++/clang++）。

## 用法

```sh
./dcc <input.c> [more.c ...] [output.asm]
```

- 单文件：`dcc myprog.c` → `myprog.asm`；或 `dcc myprog.c out.asm`。
- **多文件分离编译**：`dcc main.c lib.c out.asm` —— 每个 `.c` 独立预处理
  后合并（extern 变量/函数原型去重），输出一个 `.asm`。
  声明放头文件，实现在 `.c`：
  ```c
  /* mathlib.h */     extern int g_count;  int add(int a, int b);
  /* lib_math.c */    #include "mathlib.h"  int g_count = 0;  int add(int a, int b){...}
  /* main.c */        #include "mathlib.h"  int main(void){ return add(1,2); }
  ```
- 最后一个参数以 `.asm` 结尾视为输出；否则默认 `<第一个输入去扩展名>.asm`。
- 成功打印 `dcc: N 个输入文件 → <output> (ok)`；任一阶段（预处理/词法/语法/
  合并/代码生成）出错则输出错误并返回非零退出码。

### 完整工具链示例

```sh
# 1. C → dasm 汇编（多文件）
tools/dcc/dcc main.c lib.c myprog.asm

# 2. dasm 汇编 → 二进制
tools/dasm/dasm myprog.asm myprog.code myprog.data

# 3. 模拟器运行（SIGINT 2 秒后转储寄存器，查看 A 寄存器结果）
timeout -s INT 2 ./build/bin/doctor_sim -f code myprog.code data myprog.data
```

程序入口固定为 `_start`（TEXT 段首），它设置栈指针 `S=0x300000` 后调用 `main`，返回后 `HLT`。`main` 的返回值最终出现在 **A 寄存器**。

## 测试

```sh
sh run_dcc_test.sh            # 运行全部 test/*.c
sh run_dcc_test.sh test/t8.c  # 运行单个测试
```

每个测试源文件头注释声明期望值（`/* expect A = 0x.. */`），脚本执行
`dcc → dasm → doctor_sim` 全链路并断言 A 寄存器。当前 31 个测试覆盖：
函数调用/递归（fib）、多参数、char 参数与返回值、局部/全局变量、数组
（int/char）、循环（for/while/break/continue）、复合赋值、自增自减、
三元、短路逻辑、位运算、移位、负数与有符号比较、**signed/unsigned
类型（无符号比较/右移/除法）**、**指针（& /* /算术/下标/比较/参数）**、
**字符串（char* 指针、全局/局部字符串、下标访问）**、**signed char
符号扩展**、**short 类型**、**寄存器直访 `__reg_`**、**类型转换**、
**内联汇编 `__asm__`**、**预处理器（#include/#define/#ifdef）**、
**分离编译（头文件声明 + .c 实现）**、**自动查找 lib**、
**struct/union/enum/typedef（含链表/嵌套/整体赋值）**、
**`__interrupt__` ISR（软件中断端到端）**、**sizeof/void*/long/const/宏函数**。

## 目录结构

```
tools/dcc/
├── Makefile            # 构建脚本
├── README.md           # 本文件
├── run_dcc_test.sh     # 端到端测试脚本
├── src/                # 编译器源码（C++17）
│   ├── token.h         # 词法单元定义
│   ├── lexer.{h,cpp}   # 词法分析
│   ├── preprocessor.{h,cpp}  # 预处理器（#include / #define 等）
│   ├── ast.{h,cpp}     # 语法树与符号表
│   ├── typeenv.h       # 用户定义类型环境（struct/union/enum/typedef）
│   ├── parser.{h,cpp}  # 递归下降语法分析
│   ├── codegen.{h,cpp} # 代码生成（AST → dasm 文本）
│   └── main.cpp        # 命令行入口
├── include/            # #include <...> 默认查找目录
│   ├── mylib.h         # 示例头文件（MAX/MIN/ABS/NULL）
│   ├── grd.h           # include guard 示例
│   ├── mathlib.h       # 分离编译示例：函数原型 + extern 变量声明
│   ├── calc.h          # 自动查找 lib 示例：函数原型
│   └── io.h            # I/O 库声明（inb/inw/ind/outb/outw/outd）
├── lib/                # 自动查找的实现目录（<foo.h> → lib/foo.c）
│   ├── calc.c          # calc.h 的实现（自动查找示例）
│   └── io.c            # io.h 的实现（IN/OUT 端口访问）
├── docs/
│   ├── specification.md        # 语言子集规范
│   └── calling-convention.md   # 调用约定与代码生成规则
└── test/               # 端到端测试用例（.c）
    ├── lib_math.c      # mathlib.h 的实现（t25 的附加源文件）
    └── t25.c           # 分离编译测试（声明在头文件、实现在 .c）
```

## 支持的语言子集

完整细节见 `docs/specification.md`。要点：

- **类型**：`int`（4 字节，默认有符号）、`unsigned int`、`short`（2 字节）、
  `long`（8 字节，64 位）、`unsigned long`、`char`（1 字节，**裸 char 按无符号 0-255**）、
  `signed char`、`unsigned char`、**`void *`**（通用指针）、
  **`struct`**（结构体）、**`union`**（联合体）、**`enum`**（枚举）、
  **`typedef`**（类型别名）、**`const`**（只读限定）
- **寄存器直访**：`__reg_A` / `__reg_B` / `__reg_C` / `__reg_D1` / `__reg_D2` /
  `__reg_X` / `__reg_I` 直接读写 DOCTOR 寄存器（类型为 **`unsigned int`**，
  可作左值/右值；支持 `=`、复合赋值、`++`/`--`；右移为逻辑右移、
  除法/取余为无符号）
- **指针**：`int *` / `char *` / `unsigned int *` 等；支持 `&` 取地址、
  `*` 解引用、指针算术 `p+n`/`p-n`（按所指类型缩放）、指针下标 `p[i]`、
  指针 `++`/`--`、复合赋值 `+=`/`-=`、指针比较（`==`/`!=`/`<` 等，按地址无符号）
- **字符串**：字符串字面量类型为 `char*`；可用于 `char *p = "abc"`、
  函数参数（`char *s`）、下标 `s[i]`、`char` 数组初始化
- **语句**：表达式、`return`、`if/else`、`while`、`for`、`break`、`continue`、
  块 `{}`、变量声明（可逗号分隔多条）、**内联汇编 `__asm__`**
- **表达式**：算术 `+ - * / %`（**有符号除法/取余正确**；无符号按位运算）、
  位 `& | ^ ~`、移位 `<< >>`（**右操作数须为编译期常量**；
  有符号右移用算术右移 `MSR`、无符号用逻辑右移 `SHR`）、
  比较 `== != < <= > >=`（**两操作数均 unsigned 时用无符号比较**）、
  逻辑 `&& || !`、三元 `?:`、赋值 `=` 与复合赋值
  `+= -= *= /= %= &= |= ^= <<= >>=`、自增自减 `++ --`（前/后缀）、
  **成员访问** `s.field` / `p->field`（结构体/联合体）、
  **`sizeof`**（`sizeof(type)` / `sizeof expr`，编译期常量）、
  **强制类型转换** `(int)x` / `(char)x` / `(unsigned int)x` / `(int*)p` 等
- **数组**：`int a[5]` / `char s[6]`，下标 `a[i]`；`char` 数组可用字符串字面量初始化
- **函数**：返回值在 D1/A；参数经栈传递（见调用约定文档）；
  指针参数按值传递（传地址）

### 已知限制（v0.3）

- **`struct`/`union`**：支持定义、变量、指针、数组、成员访问（`.`/`->`）、
  嵌套 struct、整体赋值（块拷贝）；成员类型可为标量/指针/数组/嵌套 struct；
  **结构体按值传参/返回不支持**（用指针）；不支持位域/匿名结构体
- **`enum`**：支持定义与常量（含显式值、引用其它常量）；枚举变量按 int
- **`typedef`**：支持内置类型、struct/union/enum、指针的别名；数组 typedef 不支持
- **`const`**：支持只读限定（赋值给 const 变量报错）；无 `volatile`
- **`sizeof`**：支持类型与表达式（数组取总字节）；struct 查布局尺寸
- **`void *`**：通用指针（与其它指针互转需显式转换）；不能解引用
- **`long`**：64 位（8 字节）。值约定：A=低 32 位、D1=高 32 位。
  - 支持：存储/读写（小端）、`+ - * / %`（有符号/无符号）、`<< >>`（右移按符号性
    用算术/逻辑移位）、`& | ^ ~`、比较（先比高字再比低字）、`==`/`!=`、
    一元负号、类型转换（`(long)x` 符号/零扩展、`(int)long` 截断低 32 位）、
    复合赋值、`++`/`--`、函数参数与返回值（8 字节槽位）、数组/指针/结构体成员、
    全局变量（两个 `DD`）、逻辑条件（`if (long)` 取低|高）。
  - 64 位除法用移位减法（恢复余数法）实现，较慢；除数为 0 触发硬件 `#DIV`。
  - 不支持 `long long`；超出 int32 范围的整数字面量按 long 处理（如
    `0x100000000`）；超过 int64 范围的十六进制字面量会被 `strtoll` 钳位。
  - 64 位除法/取余内部临时使用 `X`/`I`（进出保存/恢复），不影响 `__reg_` 访问。
- **`__interrupt__`**：仅 `void foo(void) __interrupt__;` 形式（无参数、返回 void）；
  ISR 内不宜再触发中断（嵌套由硬件 INL 规则决定）
- **无 `{}` 数组初始化列表**：`int a[3] = {1,2,3}` 不支持（可用逐元素赋值）
- **无 `void` 指针**；`void` 仅用于函数返回类型或 `(void)` 空参数列表
- **移位右操作数必须是常量**
- **裸 `char` 按无符号处理**（0-255）；`signed char` 才是有符号
- **`__reg_` 寄存器直访**：仅支持 `A/B/C/D1/D2/X/I`（不含 S/T/F/E/R 等
  指针/栈寄存器）；类型为 `unsigned int`；寄存器无地址（`&__reg_A` 报错）。
  内部临时寄存器已避开 `X`/`I`（比较基准用 `T`、符号处理用 `R`+栈），
  但 `C` 是 `CMP` 的目标寄存器（任何比较都会写 C）、`D1/D2` 是乘除结果
  寄存器——`__reg_C`/`__reg_D1`/`__reg_D2` 的取值依赖其所在表达式
  的求值顺序
- **类型转换**：`(char)x` 截断低 8 位；int/unsigned/指针互转仅重解释、
  不改变位模式；不支持结构体转换
- **指针**：不支持函数指针、`void*`、指针与整数的隐式互转
  （`int*` 与 `char*` 赋值需显式语义，v0.2 不做转换检查）
- **`__asm__` 内联汇编**：仅支持**无操作数**形式（直接嵌入 DASM 文本），
  不支持 GNU 的输入/输出操作数约束
- **预处理器**：支持 `#include` / `#define`（对象宏/函数宏）/ `#undef` /
  `#ifdef` / `#ifndef` / `#else` / `#endif`；**不支持** `#if`/`#elif` 表达式、
  `#error`、可变参数宏、反斜杠续行跨行宏
- **分离编译**：支持多文件合并（`dcc a.c b.c out.asm`）、函数原型、`extern`
  变量；**自动查找 lib**：`#include <foo.h>` 后若其中的函数原型未被实现，
  编译器自动从 **`项目根/tools/dcc/lib/foo.c`** 加载实现（如 `<io.h>` →
  `lib/io.c`、`<calc.h>` → `lib/calc.c`）；用户显式提供的实现优先，防循环
  依赖；**不支持** `static` 文件内函数/变量、动态链接（未定义函数/变量
  报错）
- **不支持函数指针、可变参数**
- **局部变量总大小受栈空间限制**；栈顶固定 `0x300000`（避免与低地址数据区冲突）

### 预处理器 `#include` / `#define`

```c
#include <mylib.h>          /* 默认查找: 项目根/tools/dcc/include */
#include "inc/local.h"      /* 先找当前文件目录，再回退 include 目录 */

#define N 5                 /* 对象宏 */
#define SQUARE(x) ((x)*(x)) /* 函数宏 */
#define MAX(a,b) ((a)>(b)?(a):(b))

#ifdef FEATURE_ON
/* ... */
#else
/* ... */
#endif
```

- `#include <foo.h>` 默认在 **`项目根/tools/dcc/include`** 查找
  （编译时注入，运行时可用环境变量 `DCC_INCLUDE` 覆盖）；
  `#include "foo.h"` 先找当前文件所在目录，再回退 include 目录。
- `#define` 支持对象宏与函数宏；宏体在**使用处**递归展开（防自递归）；
  字符串/字符字面量内不展开；实参先展开再替换。
- `#ifdef`/`#ifndef`/`#else`/`#endif` 支持 include guard 与条件编译。
- 被包含文件同样预处理；宏表跨文件共享，条件栈每文件独立。

### 中断服务函数 `__interrupt__`

```c
void timer_isr(void) __interrupt__;   /* 原型声明为 ISR（须 void 返回、无参数） */
int ticks;

void timer_isr(void) {                /* 实现处为普通形式（属性从原型继承） */
    ticks = ticks + 1;
    return;                           /* 编译为 MOV S,F; POP F; IRET */
}
```

- 原型 `void foo(void) __interrupt__;` 标记 ISR；**必须返回 void 且无参数**（违反报错）。
- 实现处 `void foo(void) {...}` 不带修饰，合并时继承原型的 `__interrupt__` 属性。
- 函数体内 `return;` 与函数尾声均编译为 `MOV S, F; POP DWORD F; IRET`
  （ISR 不产生普通调用帧；入口先 `PUSH DWORD F` 保存被中断代码的帧指针）。
- 用软件中断 `INT N` 或硬件中断（PIT 等）触发：配置 ICT 表项指向 `func_<name>`
  即可派发（见 `tests/test_interrupt.asm` 的配置模式）。

### 内联汇编 `__asm__`

```c
__asm__("MOV A, DWORD 42");
__asm__("LET B, DWORD 0x1234\n"
        "ADD DWORD A, B");
```

- 字符串内容为 **DASM 汇编文本**（可多行、多条指令、注释 `;`）
- 相邻字符串字面量自动拼接（GNU 语义）
- 无操作数约束；寄存器/标签由你自行管理（会与 dcc 生成的代码混用寄存器，
  注意保存/恢复被 `__asm__` 修改的寄存器）
- 通过 `A` 寄存器与 C 代码交换数据

## 与 ISA 同步

dcc 生成的汇编遵循 `manual.md` 的指令契约（大小写不敏感、显式尺寸、
NZ/REP 前缀等），并依赖 dasm 的全部特性（标签、表达式、`DB/DD/RESB` 等）。
修改 ISA 时请保持 `manual.md`、模拟器（`src/`）、dasm、dcc 四处同步。

## 已知问题

- dasm 的 `DB/DW/DD` 伪指令**只接受单个数据值**（`DB <addr>, <value>`）；
  dcc 生成的数据区字符串按**每字节一行** `DB <addr>, 0xXX` 输出
- dasm 预处理会在**字符串内**剥离 `;` 后的内容；dcc 生成的数据区字符串
  一律以十六进制字节输出以规避此限制
- 生成的代码中 `_start` 直接调用 `func_main`；若程序未定义 `main`，
  dasm 会报未定义标号（预期行为）
