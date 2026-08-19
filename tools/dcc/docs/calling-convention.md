# dcc 调用约定与代码生成规则 (v0.1)

本文档记录 dcc 生成的 DOCTOR 汇编代码所遵循的调用约定与关键代码生成
规则。这些规则已由 `test/t8.c`（递归斐波那契）等端到端测试验证。

## 1. 寄存器使用约定

| 寄存器 | 用途 |
|---|---|
| `A` | 表达式求值结果；函数返回值（调用后 `MOV A, D1` 取得）；**long 低 32 位** |
| `B` | 二元运算右操作数、左值地址（`gen_lvalue_addr`）；**long 右操作数低字** |
| `C` | 比较寄存器（`CMP` 目标、条件跳转的操作数）；64 位除法余数低字 |
| `D1` | 函数返回值通道（`return` 时 `MOV D1, A`；调用方 `MOV A, D1`）；**long 高 32 位** |
| `D2` | `MUL` 低 32 位结果、`DIV` 商；64 位除法余数高字/循环计数 |
| `F` | 帧指针（`SFA` 建立，**被调函数覆盖，调用方保存**） |
| `S` | 栈指针（向高地址增长） |
| `E` | 跳转目标（`JMP`/条件跳转前必须 `LET E, DWORD target`；**不可作普通暂存**——写 E 触发代码区检查） |
| `R` | 字符串拷贝循环源指针（`LOD`）；**long 右操作数高字** |
| `X`/`I` | 64 位除法内部余数暂存（进出保存/恢复；用户 `__reg_X/I` 不受影响） |
| `T` | 恒 0（部分比较/清零基准） |

## 2. 栈帧布局

DOCTOR 的 `PUSH` 语义为 `S++; *S = value`（**先增后写**），DWORD 压栈
连续执行 4 次自增并写入 `[S_old+1 .. S_old+4]`。因此栈上每个 4 字节
元素相对"对齐起点"偏移 1 字节——这是本约定所有偏移公式的基准。

### 调用序列（调用方）

```
; 假设调用 f(arg0, arg1, ..., argn-1)
MOV A, F
PUSH DWORD A        ; [F 保存] 调用方帧指针（被调函数的 SFA 会覆盖 F）
<求 arg0> PUSH DWORD A          ; 4 字节参数
<求 arg1> PUSH DWORD A          ; 4 字节参数
...
<求 argn-1> PUSH DWORD A
LET E, DWORD RETn
PUSH DWORD E        ; 返回地址（最后压入，位于栈顶）
LET E, DWORD func_f
JMP
RETn:
SUB DWORD S, <argbytes>  ; 调用方清栈（argbytes = Σ 参数槽位字节）
POP DWORD F         ; 恢复调用方帧指针
MOV A, D1           ; 取得返回值（仅 int/char/指针返回；long 返回值已在 A:D1）
```

- **long 参数占 8 字节槽位**：先压低字 `PUSH DWORD A`，再压高字 `PUSH DWORD D1`
  （内存小端序：低字在低地址）。int 实参在压栈前先提升为 long（符号/零扩展 D1）。
- 调用前 `S=S0`，进入被调函数时 `S = S0 + argbytes + 4`。
- **long 返回值**：被调函数直接返回 A(低):D1(高)，调用方**不再** `MOV A, D1`。

### 函数入口（被调方）

```
func_f:
SFA DWORD <frame>   ; F = S; S += frame
```

- `F` 指向返回地址区（`*F` 附近存返回地址，`RER` 用它弹回）。
- 参数 `i`（0 起，左→右）首地址 = `F - (3 + Σ_{j≥i} slot(j))`，
  `slot(long)=8`、其余=4：
  - 参数 0（第一个）在 `F - (3 + Σ所有槽位)`
  - 参数 n-1（最后一个）在 `F - (3 + slot(n-1))`（int 参数在 `F - 7`）
  - 原因：第 i 个参数实际写在 `S0 + 4i + 1`，而 `F = S0 + 4n + 4`，
    相减得 `4n + 4 - 4i - 1 = 4n - 4i + 3`。
- 局部变量从 `F + 4` 起（`F` 处 4 字节为返回地址区），按声明顺序递增：
  第一个局部 `F+4`，第二个 `F+8`……
- `SFA` 的 `frame` 值 = 局部变量区顶端（`F + 4 + 局部总字节数`），
  保证函数内临时 `PUSH`（写 `[S+1..S+4]`）不会覆盖局部变量。

### 函数返回（被调方）

```
MOV D1, A      ; 返回值装入 D1
RER            ; S = F; POP E（恢复返回地址；S 回落 4）
JMP            ; 跳回调用方
```

## 3. 表达式求值规则

- **结果恒在 A**。
- 二元运算：求右 → `PUSH DWORD A` → 求左（在 A）→ `POP DWORD B`
  （B = 右）→ 运算（`ADD/SUB/MUL/DIV/AND/OR/XOR ... A, B`）。
  - `MUL DWORD A, B` → 64 位积在 `D1:D2`，低 32 位在 `D2` → `MOV A, D2`
  - `DIV DWORD A, B` → 商 `D2`、余数 `D1`；取商 `MOV A, D2`，取余 `MOV A, D1`
- **比较**：`MOV C, A; CMP DWORD B`（C = A - B），随后：
  - `==` → `JZ`（C==0）
  - `!=` → `JNZ`
  - `<` → `JL DWORD X`（先 `ZERO X`）
  - `<=` → `JNG DWORD X`
  - `>` → `JG DWORD X`
  - `>=` → `JNL DWORD X`
  - 比较产生 0/1 到 A（`gen_compare` 用两路跳转）。
- **逻辑真值**（if/while 条件）：表达式求值后 `MOV C, A; ZERO X;
  CMP DWORD X; JZ`（C 与 0 比较）。
- **`&&`/`||`** 短路：求左 → 与 0 比较 → 决定是否求右 → 归并 0/1。
- **三元 `c ? a : b`**：求 c → 与 0 比较 → 两路跳转求 a 或 b。
- **复合赋值 `a op= b`**：
  ```
  <左值地址> → B
  PUSH DWORD B              ; [addr]
  LR DWORD A, *B            ; A = old
  PUSH DWORD A              ; [addr][old]
  求 b                      ; A = 右
  POP DWORD B               ; B = old
  ; 运算（注意非交换运算的顺序：A=右, B=old）
  ADD DWORD A, B            ; +=（指针：B += A*sizeof(*p)）
  SUB DWORD B, A; MOV A, B  ; -=
  MUL DWORD A, B; MOV A, D2 ; *=
  ; /= %= unsigned：DIV B, A（old / 右）
  ; /= %= signed：交换 A/B → 有符号除法序列（见 §5.1）
  SHL DWORD A, <const>      ; <<= （右须为常量）
  POP DWORD B               ; B = addr
  ST DWORD *B, A            ; 存回
  ```
- **`++`/`--`**：
  ```
  <左值地址> → B
  PUSH DWORD B              ; [addr]
  LR DWORD A, *B            ; A = old
  [后缀] PUSH DWORD A       ; [addr][old]
  ADD/SUB DWORD A, <step>   ; 标量 step=1；指针 step=sizeof(*p)
  [后缀] POP B(old); POP C(addr); ST *C, A; MOV A, B   ; 结果=旧值
  [前缀] POP B(addr); ST *B, A                          ; 结果=新值
  ```

## 4. 变量寻址

| 变量 | 地址计算 |
|---|---|
| 全局标量/数组 | `LET A/B, DWORD var_<name>` |
| 局部变量（F 上方） | `MOV A/B, F; ADD DWORD A/B, <offset>`（offset 从 4 起） |
| 参数（F 下方） | `MOV A/B, F; SUB DWORD A/B, <4*(n-i)+3>` |
| 数组元素 `a[i]` | 下标 → 缩放（int×4 / char×1 / 指针×sizeof）→ `PUSH`；基址 → `B`；`POP C; ADD DWORD B, C`；读取 `MOV A,B; LR ...` / 写入 `ST ...` |
| 指针元素 `p[i]` | 基址=指针值 + 下标×sizeof(*p) |
| `*p` 左值 | 地址 = p 的值（`gen_expr(p); MOV B, A`） |

- 读取标量：`LR DWORD A, *A`（int）/ `LR BYTE A, *A`（char）
  - **signed char 读取后符号扩展**：`SHL DWORD A, 24; MSR DWORD A, 24`
  - unsigned char 直接 `LR BYTE`（高 24 位自动为 0）
- 写入标量：`ST DWORD *B, A`（int）/ `ST BYTE *B, A`（char）
- 指针变量本身 4 字节，按 DWORD 读写（`LR DWORD A, *A` 取指针值）。
- 指针算术缩放：`p ± n` 中 n 先乘以 `sizeof(*p)`（1/2/4/8 用 `SHL`，
  其它尺寸用临时寄存器乘法）。

## 5. 有符号除法/取余（内联序列）

DOCTOR 的 `DIV` 是无符号的。dcc 对**有符号** `/` `%` 生成内联符号处理：

```
; 输入 A=被除数, B=除数；输出 D2=商, D1=余
; 寄存器：X=被除数符号, R=除数符号, I=商符号, T=恒 0
ZERO T
; X = (A<0) ? 1 : 0
MOV C, A; CMP DWORD T; LET E, L1; JNL DWORD T
LET X, DWORD 1; LET E, L2; JMP
L1: ZERO X
L2:
; R = (B<0) ? 1 : 0   （同上模式）
; I = X ^ R（商符号）
MOV I, X; XOR DWORD I, R
; |A|、|B|（负数 MNE）
MOV C, A; CMP DWORD T; LET E, L3; JNL DWORD T
MNE DWORD A
L3:
; （|B| 同上）
DIV DWORD A, B          ; 无符号除
; 商修正：if (I) D2 = -D2
MOV C, I; CMP DWORD T; LET E, L4; JZ
MNE DWORD D2
L4:
; 余修正：if (X) D1 = -D1（余数符号同被除数）
MOV C, X; CMP DWORD T; LET E, L5; JZ
MNE DWORD D1
L5:
```

### 5.1 比较符号规则

- `==`/`!=`：`JZ`/`JNZ`（与符号无关）。
- `< <= > >=`：**两操作数均 unsigned** → `JB/JNB/JA/JNA`；
  否则（含指针比较）→ `JL/JNG/JG/JNL`。
- 比较前 `MOV C, A; CMP DWORD B`（C = A - B）。

### 5.2 字符串常量输出

dasm 预处理会剥离 `;` 后的内容（即使字符串内），且 dasm 的
`DB/DW/DD` **只接受单个数据值**，因此 dcc 的字符串常量按
**每字节一行**输出：

```
str0:
    DB 0, 0x68
    DB 1, 0x69
    DB 2, 0x00
```

`char *g = "abc"` 全局指针：`DD <offset>, str0`（字符串常量紧跟其后，
dasm 支持前向引用）。

## 6. 字符数组字符串初始化（局部）

```
; char s[n] = "str";  拷贝 len = min(strlen+1, n) 字节
LET R, DWORD <str 常量标号>   ; 源（DATA 段）
<目标地址> → B
LET C, DWORD <len>             ; 计数器
loop:
LOD BYTE A                     ; A = *R; R++
ST BYTE *B, A                  ; *B = A
INC B
CDI                            ; C--
LET E, DWORD loop
JNZ                            ; C != 0 继续
```

## 7. 入口与结束

```
_start:
    LET S, DWORD 0x300000    ; 栈顶（16MB 数据区内的安全位置）
    MOV A, F
    PUSH DWORD A             ; 保存 F（初始 0）
    LET E, DWORD RET0
    PUSH DWORD E
    LET E, DWORD func_main
    JMP
RET0:
    POP DWORD F
    HLT
```

- `main` 返回值最终在 **A 寄存器**（`_start` 不做额外搬运）。
- `0x300000` 避开低地址数据区（全局变量从 0 起）。

## 8. 内联汇编 `__asm__`

- 无操作数形式：`__asm__("dasmasm 文本");`，文本按行原样嵌入
  TEXT 段当前位置（dasm 语法）。
- 相邻字符串自动拼接；`;` 后为 dasm 注释。
- 程序员负责保存/恢复被修改的寄存器（dcc 不感知 asm 内部行为）。

## 9. 寄存器直访 `__reg_` 与类型转换的生成

### 9.1 寄存器直访 `__reg_A` 等

- 类型恒为 `unsigned int`（位模式）。
- 右值：`MOV A, <reg>`（如 `__reg_X` → `MOV A, X`）。
- 简单赋值 `__reg_X = v`：`<求 v 到 A>` → `MOV X, A`。
- 复合赋值 `__reg_X += v`：`MOV A, X` → `PUSH` → 求 v → `POP B`（B=old）
  → 运算（结果到 A）→ `MOV X, A`。
  - `/` `%`：无符号 `DIV DWORD B, A`（B=old 被除数、A=右 除数）
  - `>>`：逻辑右移 `SHR`；`<<`：`SHL`
- `++`/`--`：`MOV A, <reg>` → `ADD/SUB DWORD A, 1` → `MOV <reg>, A`；
  后缀先 `PUSH` 旧值，结束后 `POP` 恢复为表达式结果。

> 内部寄存器隔离：比较基准用 `T`（用户不可访问）、有符号除法符号用
> `R`+栈，避免覆盖 `__reg_X`/`__reg_I`。`C`（CMP 目标）与 `D1/D2`
> （乘除结果）为 ISA 语义必然使用的寄存器。

### 9.2 强制类型转换 `(type)expr`

- 求值 expr 到 A 后：
  - 目标为 `char`/`unsigned char` 且源非 char：`SHL DWORD A, 24; SHR DWORD A, 24`
    （截断低 8 位，逻辑右移清零高位）。
  - 其余（int/unsigned/指针互转）：无操作（位模式不变）。
- `(int*)`、`(char*)` 等指针转换：无操作。

## 10. 已验证的边界情况

- 递归（fib）：F 保存/恢复、参数偏移、清栈在多层调用下正确。
- 多参数（4 个）：参数 i 偏移公式 `F - (4*(n-i)+3)` 正确。
- signed 除法/取余：`-36/4=-9`、`14%5=4`、负数除数等。
- signed/unsigned 右移：`-8>>1=-4`（MSR）、`0x80000000>>1=0x40000000`（SHR）。
- 指针：`p+1` 步进 sizeof、`p[i]`、`*p` 读写、指针比较、递归传数组指针。
- 字符串：`char *p="hi"`、函数参数传字符串、`s[i]` 访问、strlen 模拟。
- signed char：`-5` 读取符号扩展后比较正确。
- char 参数/返回值：按 4 字节 DWORD 传参，函数内按 char 截断使用。
- 复合赋值除法/取余：`DIV DWORD B, A`（old/右）顺序正确。
- 有符号比较：依赖模拟器 `JL/JG/JNG/JNL` 的 int32 语义
  （模拟器已修复 `&mask` 导致的比较错误）。
