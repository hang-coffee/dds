# TODO / 已知限制

> 当前 `dcc` 已经是功能更完整的 C99 重写版。以下清单区分“已完成”和“尚未支持”。

## 已完成的 C99 / 扩展特性

### 类型系统

- [x] `short` / `long` / `long long`（`long`/`long long` 使用 8 字节槽位，支持 64 位加减乘除/比较/位运算/移位，含符号区分）
- [x] `unsigned` / `signed` 限定符（64 位比较/右移/除法已区分有符号与无符号）
- [x] `float` / `double` / `long double`（`long double` 为 80 位 DXE；存储、赋值、四则运算、比较、函数传参/返回、与整数/浮点互转）
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
- [x] 匿名结构体 / 联合体（成员直接并入父结构体/联合体）
- [x] 位域（基础读写，按 4 字节容器打包）

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
- [x] 可变参数宏 `__VA_ARGS__`
- [x] `switch` 支持 C 的 case 穿透（fallthrough）
- [x] 字符字面量（`'A'`、`'\n'` 等）与八进制字面量（`052`）
- [ ] 完整 C 标准库（已提供 freestanding 常用头文件：`stdarg.h`、`stddef.h`、`stdbool.h`、`stdint.h`、`inttypes.h`、`io.h`、`math.h`、`string.h`）

### 代码生成 / 工具链

- [x] ELF 目标文件输出（`-m elf`）
- [x] 平坦二进制输出（`-m bin`）
- [x] 多文件编译（`dcc a.c b.c out.asm`，含自动查找并合并 lib 实现）
- [x] 内联汇编（`__asm__`）与寄存器直访（`__reg_*`）
- [x] 结构体按值返回（通过 `struct_ret` 缓冲）
- [ ] 优化
- [ ] 调试信息
- [x] 结构体按值传参

### 具体的优化措施

> 目前 `dcc` 只有 **-O0**（基线：直译式代码生成，不优化）。规划按
> `-O1 / -O2 / -O3 / -Os` 分级，每一级叠加前一级的优化。所有优化以
> DOCTOR ISA 原生特性为落脚点（`LR/ST *F+N` 偏移寻址、`INC/DEC`、`ZERO`、
> `SHL/SHR/MSR`、`LOD/STO`、`SR` 三操作数寻址、`TRA` 块复制、`BLKS` 批量填充）。

#### -O0（现状基线）

- [x] 直译式生成，不优化。当前每个局部变量读写都产生冗长序列：
  - 读局部：`MOV A,F; ADD DWORD A,off; LR DWORD A,*A`（3 条）
  - 写局部：`PUSH DWORD A; MOV A,F; ADD DWORD A,off; MOV B,A; POP DWORD A; ST DWORD *B,A`（6 条）

#### -O1：局部优化（目标：代码更短、更规整）

- [x] **帧相对寻址**：局部变量/参数改用 `LR DWORD A, *F+N` / `ST DWORD *F+N, A`
      直接寻址（ISA 已支持 `*reg+N` 偏移，按尺寸符号扩展，可写 `*F-4`）。消除
      `MOV A,F; ADD; MOV B,A; POP` 等冗余，单条读写指令取代 3~6 条。
      - 已覆盖：变量读取（`emit_load_var`）、`x = expr` 赋值、声明初始化
        `int x = expr;`、自增/自减 `x++/x--`。
      - 限制：仅 `LR`/`ST` 支持 `*F±N` 偏移（`FLD/FST/DLD/DST/ELD/EST` 不支持），
        浮点/长双精度读写仍走地址计算路径；BYTE/WORD 偏移受符号扩展范围
        （±127 / ±32767）约束，超限自动回退旧路径。复合赋值仍走旧路径。
- [x] **常量折叠**：整型常量表达式（`+ - * / % << >> & | ^`、比较、一元 `- ~ !`）
      在编译期求值，直接生成 `LET A, DWORD 0x…`（64 位同时写 `D1`；0 生成
      `ZERO A`）。除零 / 非法移位回退为运行时求值（保持原语义）。
      实现：`expr_const_int()` / `emit_const_result()`，挂接 `gen_expr` 的
      `EXPR_BIN`/`EXPR_UNARY`；移位量也支持折叠（`x << (1+2)`）。
      - 已覆盖：`2+3`→`0x5`、`(1+2)*(3+4)`→`0x15`、`1<<(2+2)`→`0x10`、
        `-5`→`0xFFFFFFFB`、`~0`→`0xFFFFFFFF`、`10u/3u`→`3`、64 位常量等。
- [ ] **强度削减**：`ADD x,1 → INC`、`SUB x,1 → DEC`、`MUL x,2^k → SHL`、
      `DIV x,2^k → SHR/MSR`（有符号用 MSR）、`ADD/SHL x,0` 与 `MUL x,1` 删除。
- [x] **简单 peephole**（对生成的 .asm 做行级后处理，`peephole_asm_file`）：
      - `LET reg,DWORD 0 → ZERO reg`（短编码）
      - `MOV r,r` 删除
      - 相邻 `MOV a,b; MOV b,a`（或完全相同）→ 删第二个（冗余）
      - 未做：`AND/OR/XOR` 与全 0/全 1 归约、`PUSH/POP` 平衡检查。
- [ ] **立即数操作数**：ISA 第二操作数可直接为立即数（编码 `0xF`），生成
      `ADD DWORD A, N` 而非 `LET B,N; ADD DWORD A,B`。
- [ ] **跳转优化**：消除 `JMP` → `JMP` 链式跳转；条件跳转直接使用 `LET E, label`
      紧邻的短跳转形态。

#### -O2：数据流优化

- [ ] **寄存器分配**：跨语句分配 `D1/D2/R/X/I` 为临时寄存器，减少 `PUSH/POP`
      溢出（当前几乎全部经 A 中转 + 栈保存）。
- [ ] **常量传播 / CSE**：同一地址计算（`MOV A,F; ADD` 等）只计算一次；公共
      子表达式复用，避免重复求值。
- [ ] **死代码消除**：删除未被读取的赋值、无用标号、不可达分支与空 `if/else`。
- [ ] **块复制 / 块填充**：结构体整体赋值、数组复制改用 `TRA`（`*b=*a` 已支持，
      推广到结构体/数组）；数组清零用 `BLKS`。
- [ ] **地址计算合并**：`base + idx*k + off` 编译为单条 `SR`（ISA 原生三操作数
      寻址），取代 `MUL; ADD` 组合与多余暂存。
- [ ] **循环优化**：循环不变式外提（把与循环无关的 `*F+N` 地址计算移出）；
      数组遍历改用 `LOD/STO`（自增 R）替代 `LR/ST + INC R`。

#### -O3：更激进

- [ ] **函数内联**（`inline` / 小函数）：消除 `PUSH/POP` 参数传递与 `SFA/RER`
      帧建立开销。
- [ ] **尾调用优化**：`return f(...)` 复用当前栈帧，直接 `JMP`，省去 `RER`。
- [ ] **循环展开 / 软件流水**：小常数循环展开，减少 `CSI/CDI` 与分支开销。
- [ ] **PUSH/POP 批量调度**：相邻多对 `PUSH A; ...; POP A` 合并或重排，减少栈操作。

#### -Os：面向代码体积

- [ ] 优先短编码：BYTE 尺寸立即数、`INC/DEC`、`ZERO`、`TRA`、`BLKS`、`SR` 等
      短指令优先于通用长序列。
- [ ] 公共子序列提取：跨函数的相同指令序列合并（尾部合并）。
- [ ] 条件分支布局：按"热路径在前"重排 `if/else`、循环体，减少跳转距离。

> 落地建议：`-O1` 的**帧相对寻址**收益最大（局部读写由 3~6 条降到 1 条），
> 建议优先实现，并以 `tests/` 现有用例 + `code.bin` 体积/指令数作为回归基线。
