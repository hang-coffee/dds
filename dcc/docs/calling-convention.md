# dcc 调用约定与代码生成规则（C99 重写版）

本文档记录 dcc 生成的 DOCTOR 汇编代码所遵循的调用约定与关键代码生成规则。
这些规则由 `tests/` 下的端到端测试（函数调用、长整型、浮点、结构体返回、
ISR 等）验证。

## 1. 寄存器使用约定

| 寄存器 | 用途 |
|---|---|
| `A` | 表达式求值结果；函数返回值通道（普通标量）；**long/long long 低 32 位** |
| `B` | 二元运算右操作数、左值地址（`gen_lvalue_addr`）；**long 右操作数低字** |
| `C` | 比较寄存器（`CMP` 目标）；64 位除法余数低字/循环计数 |
| `D1` | 函数返回值通道（普通标量返回时 `MOV D1, A`；调用方 `MOV A, D1`）；**long 高 32 位** |
| `D2` | `MUL` 低 32 位结果、`DIV` 商；64 位除法余数高字/循环计数 |
| `F` | 帧指针（`SFA` 建立，被调函数覆盖，调用方保存） |
| `S` | 栈指针（向高地址增长） |
| `E` | 跳转目标（`JMP`/条件跳转前必须 `LET E, DWORD target`；不可作普通暂存） |
| `R` | 字符串/结构体拷贝循环源指针（`LOD`）；**long 右操作数高字** |
| `X`/`I` | 64 位除法内部余数暂存（进出保存/恢复；用户 `__reg_X/I` 不受影响） |
| `T` | 恒 0（比较/清零基准） |
| `FP0`/`DP0` | `float`/`double` 表达式结果与函数返回 |
| `FP1`/`DP1` | 浮点二元运算的右操作数 |

## 2. 栈帧布局

DOCTOR 的 `PUSH` 语义为 `S++; *S = value`（先增后写），DWORD 压栈连续执行 4 次
自增并写入 `[S_old+1 .. S_old+4]`。因此栈上每个 4 字节元素相对“对齐起点”偏移
1 字节——这是本约定所有偏移公式的基准。

### 调用序列（调用方）

```
; 假设调用 f(arg0, arg1, ..., argn-1)
MOV A, F
PUSH DWORD A        ; [F 保存] 调用方帧指针
<求 arg0> PUSH DWORD A          ; 4 字节参数
<求 arg1> PUSH DWORD A
...
<求 argn-1> PUSH DWORD A
LET E, DWORD RETn
PUSH DWORD E        ; 返回地址（最后压入，位于栈顶）
LET E, DWORD func_f
JMP
RETn:
SUB DWORD S, <argbytes>  ; 调用方清栈
POP DWORD F         ; 恢复调用方帧指针
MOV A, D1           ; 取得普通标量返回值
```

- **long/long long 参数占 8 字节槽位**：先压低字 `PUSH DWORD A`，再压高字
  `PUSH DWORD D1`（内存小端序）。int 实参在压栈前先提升为 64 位（符号/零扩展 D1）。
- **float 参数**：`FPUSH FP0`（4 字节槽位）。
- **double 参数**：`DPUSH DP0`（8 字节槽位）。
- **long/long long 返回值**：被调函数直接返回 A(低):D1(高)，调用方不再 `MOV A, D1`。
- **float/double 返回值**：留在 `FP0`/`DP0`，调用方不再搬运。
- **结构体返回值**：被调函数把数据拷贝到全局 `struct_ret`，调用方取
  `LET A, DWORD struct_ret` 得到其地址。

### 函数入口（被调方）

```
func_f:
SFA DWORD <frame>   ; F = S; S += frame
```

- `F` 指向返回地址区。
- 参数 `i`（0 起，左→右）首地址 = `F - (3 + Σ_{j≥i} slot(j))`，
  `slot(long/double)=8`、其余=4。
- 局部变量从 `F + 4` 起，按声明顺序递增。
- `SFA` 的 `frame` 值 = 局部变量区顶端（`F + 4 + 局部总字节数`）。

### 函数返回（被调方）

普通标量：

```
MOV D1, A      ; 返回值装入 D1
RER            ; S = F; POP E
JMP
```

64 位返回：A:D1 已就绪，直接 `RER; JMP`。
浮点返回：FP0/DP0 已就绪，直接 `RER; JMP`。
结构体返回：先拷贝到 `struct_ret`，再 `RER; JMP`。
ISR 返回：`MOV S, F; POP DWORD F; IRET`（不执行普通 RER/JMP）。

## 3. 表达式求值规则

- 普通标量结果恒在 A；浮点结果在 FP0/DP0；64 位结果在 A:D1。
- 二元运算：求右 → `PUSH DWORD A` → 求左（在 A）→ `POP DWORD B` → 运算。
  - 64 位：右操作数低字 B、高字 R；左操作数低字 A、高字 D1。
  - 浮点：右操作数 FP1/DP1，左操作数 FP0/DP0，用 `FADD/FSUB/FMUL/FDIV` 或
    `DADD/DSUB/DMUL/DDIV`。
- 比较产生 0/1 到 A（两路跳转）。
- 逻辑真值（if/while 条件）：表达式求值后与 0 比较。
- `&&`/`||` 短路；三元两路跳转。
- 复合赋值 `a op= b`：
  ```
  <左值地址> → B
  PUSH DWORD B              ; [addr]
  LR DWORD A, *B            ; A = old
  PUSH DWORD A              ; [addr][old]
  求 b                      ; A = 右
  POP DWORD B               ; B = old
  ; 运算（注意非交换运算的顺序：A=右, B=old）
  POP DWORD B               ; B = addr
  ST DWORD *B, A            ; 存回
  ```
- `++`/`--`：标量 step=1；指针 step=`sizeof(*p)`；前/后缀都支持。

## 4. 变量寻址

| 变量 | 地址计算 |
|---|---|
| 全局标量/数组 | `LET A/B, DWORD var_<name>` |
| 局部变量（F 上方） | `MOV A/B, F; ADD DWORD A/B, <offset>`（offset 从 4 起） |
| 参数（F 下方） | `MOV A/B, F; SUB DWORD A/B, <4*(n-i)+3>`（8 字节参数按槽位叠加） |
| 数组元素 `a[i]` | 下标缩放 → 基址 → 相加 |
| `*p` 左值 | 地址 = p 的值 |

- 读取标量：`LR DWORD`（int/指针）、`LR WORD`（short）、`LR BYTE`（char）。
  - signed char/short 读取后符号扩展。
- 写入标量：`ST DWORD/WORD/BYTE`。
- 64 位读写：连续两个 DWORD，低字在前。
- 浮点读写：`FLD/ FST` 或 `DLD/DST`。

## 5. 有符号除法/取余（内联序列）

DOCTOR 的 `DIV` 是无符号的。dcc 对 32 位有符号 `/` `%` 生成内联符号处理：
取绝对值 → 无符号除 → 按符号修正商和余数。

- 余数符号同被除数。
- 64 位除法/取余使用移位减法（恢复余数法）。
- 指针比较按地址（无符号）；两操作数均 unsigned 用无符号跳转。

## 6. 字符串常量输出

dasm 预处理会剥离 `;` 后的内容（即使字符串内），且 dasm 的 `DB/DW/DD` 只接受
单个数据值，因此 dcc 的字符串常量按**每字节一行**输出：

```
str0:
    DB 0, 0x68
    DB 1, 0x69
    DB 2, 0x00
```

`char *g = "abc"` 全局指针：`DD <offset>, str0`。

## 7. 入口与结束

dcc 不再生成 `_start`。用户自行汇编/链接 `bootable_crt.asm` 或 `bin_crt.asm`。

`bootable_crt.asm` 的入口逻辑：

```
_start:
    LET S, DWORD 0x300000    ; 栈顶（避开低地址数据区）
    MOV DWORD A, F
    PUSH DWORD A             ; 保存 F（初始 0）
    LET E, DWORD _crt_ret0
    PUSH DWORD E
    LET E, DWORD func_main
    JMP
_crt_ret0:
    POP DWORD F
    HLT
```

- `main` 返回值最终在 A 寄存器。
- `bin_crt.asm` 不设置 S，只跳转 `func_main`。

## 8. 内联汇编 `__asm__`

- 无操作数形式：`__asm__("dasmasm 文本");`，文本按行原样嵌入 TEXT 段当前位置。
- 相邻字符串自动拼接；`;` 后为 dasm 注释。
- 程序员负责保存/恢复被修改的寄存器（dcc 不感知 asm 内部行为）。

## 9. 寄存器直访 `__reg_` 与类型转换的生成

### 9.1 寄存器直访 `__reg_A` 等

- 类型恒为 `unsigned int`（位模式）。
- 右值：`MOV A, <reg>`。
- 简单赋值 `__reg_X = v`：求 v → `MOV X, A`。
- 复合赋值：先读寄存器到 A，再与右值运算，最后写回。
- `/` `%` 为无符号 `DIV`；`>>` 为逻辑右移。
- `++`/`--`：`ADD/SUB DWORD A, 1` 后写回。

### 9.2 强制类型转换 `(type)expr`

- 目标 `char`/`unsigned char`：`SHL DWORD A, 24; SHR DWORD A, 24`。
- 目标 `short`/`unsigned short`：`SHL DWORD A, 16; SHR DWORD A, 16`。
- int/unsigned/指针互转：位模式不变。
- float/double 之间及与整数之间：使用 DFE/DDE 转换指令。

## 10. 已验证的边界情况

- 函数递归：F 保存/恢复、参数偏移、清栈在多层调用下正确（调用约定支持递归）。
- 多参数：参数 i 偏移公式正确。
- signed 除法/取余：负数除数、余数符号等。
- signed/unsigned 右移：`-8>>1=-4`（MSR）、`0x80000000>>1=0x40000000`（SHR）。
- 64 位加法/减法/乘法/除法/比较/移位。
- 浮点：float/double 四则运算、比较、int↔float↔double 转换。
- 指针：`p+1` 步进 sizeof、`p[i]`、`*p`、指针比较、递归传数组指针。
- 字符串：`char *p="hi"`、函数参数传字符串、`s[i]` 访问。
- signed char：读取符号扩展后比较正确。
- 结构体：整体赋值、按值返回、指针返回、嵌套成员。
- ISR：`MOV S,F; POP F; IRET` 尾声。
