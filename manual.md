# DOCTOR

*DOCTOR Owns a Complete insTructions set, Operates dual addResses.*

**ARCHITECTURE v3.3**

## 概览

**DOCTOR** 是一套为清晰表达意图而设计的低级指令集架构。它采用 **哈佛架构**（代码与数据空间分离），**Little‑Endian** 字节序，指令为 **变长编码**（至少 2 字节，长度由第一个字节的低 4 位指定）。  
**所有涉及内存或 I/O 的操作应当显式指定尺寸**（`BYTE` / `WORD` / `DWORD`），尺寸编码在指令首字节的 bit 6–5 中。  
支持“高位保留”变体（`NZ` 后缀），由首字节 bit 4 控制。

## 寄存器

### 代码指针
- **P** Program (隐式自增, 不可写)
- **E** codE (可写跳转目标)
### 数据指针
- **S** Stack
- **T** Task Base
- **A** Accumulator 
- **B** Backup 
- **F** Frame
- **R** addRess Pointer

### 数据寄存器
- **A** Accumulator
- **B** Backup
- **C** Counter
- **D1** Data 1
- **D2** Data 2
- **R** addRess Pointer
- **X** eXtra
- **I** consIstent Pointer (可以用于方便存放基址、常量等内容)

### 系统寄存器
- **RIN1** Register for INterrupts 1 (32位，中断使能寄存器，bit i表示中断base+i被允许响应)
- **RIN2** Register for INterrupts 2 (32位，中断挂起寄存器，bit i表示中断base+i有请求正在等待)
- **RIN3** Register for INterrupts 3 (32+32位，高32位是中断控制表(ICT)的代码基址ICTB，低32位是系统控制寄存器CTRL)
- **CBASE** Code BASE (32位，表示用户代码区的最低合法地址)
- **CLIMIT** Code LIMIT (32位，表示用户代码区的最高合法地址)
- **DBASE** Data BASE (32位，表示用户数据区的最低合法地址)
- **DLIMIT** Data LIMIT (32位，表示用户数据区的最高合法地址)
- **XAR** eXception AddRess (32位，表示上一次触发的异常的相关信息，硬件复位值为0)

其中，系统寄存器仅在内核态中可以读写。

下面是`RIN3`寄存器的定义：（从左到右是从高到低）
| 位 | bit 63 - bit 32 | bit 31 | bit 30 | bit 29 | bit 28 | bit 27-24 | bit 23-16 | bit 15-0 |
|---|---|---|---|---|---|---|---|---|
| 名称 | ICTB | GIE | INL | MPU | CPL | Rsvd | ISRB | Rsvd
| 功能 | ICT基址 | 全局中断使能(1开0关), 由PUSHI和POPI管理 | 中断嵌套锁，只读，中断响应时为1，IRET清零 | MPU开关，1开0关 | 特权级，0为内核，1为用户 | 保留 | 由RIN1与RIN2管理的32个外部中断的起始中断号（也就是IRQ 0所对应的中断号） | 保留

硬件复位值：`RIN3`=`0x0000_0000` + `0x0000_0000`（中断关闭，内核态，护栏关闭）

在中断发生时，硬件会压栈：（依次）RIN3, P, 当前的中断号

在中断结束时，硬件会弹栈：（依次）中断号, P, RIN3

中断结束后的返回应当使用`IRET`命令。系统会自动清除RIN2当前的中断位。

软件中断由`INT [N/DR]` 触发。与硬件中断不同，软件中断是程序的显式请求，**不受全局中断关闭的影响**：即使 GIE=0 或处于 PUSHI/POPI 对内部，`INT` 也总是尝试派发；若因嵌套规则（当前 ISR 不允许嵌套或优先级不足）或越权（ISR_DPL 检查）被拒，则触发 #GP。

全局中断关闭的两个条件为：在PUSHI/POPI内部，或GIE为0。前者优先级比后者高。（只影响硬件中断与 RIN2 挂起的中断）

所有硬件中断都是边沿触发的。

代码示例：

```
GETB D1, RIN3_CTRL
LET DWORD X, 0x7FFF_FFFF		; 关闭GIE
AND DWORD D1, X
SETB RIN3_CTRL, D1				; 此时中断被禁止

GETB D1, RIN3_CTRL
LET DWORD X, 0x8000_0000		; 开启GIE
OR DWORD D1, X
SETB RIN3_CTRL, D1				; 此时中断被允许

PUSHI							; 注意：此时中断被禁止
GETB D1, RIN3_CTRL
LET DWORD X, 0x8000_0000		; 开启GIE
OR DWORD D1, X
SETB RIN3_CTRL, D1				; 此时中断仍被禁止
POPI							; 系统恢复到GIE设定的状态——开启中断

PUSHI							; 注意：此时中断被禁止
GETB D1, RIN3_CTRL
LET DWORD X, 0x7FFF_FFFF		; 关闭GIE
AND DWORD D1, X
SETB RIN3_CTRL, D1				; 此时中断仍被禁止
POPI							; 系统恢复到GIE设定的状态——禁止中断
```

下面是发生异常时自动触发的中断。在触发后，只会跳转到ICT表项的高4字节（ISR_BASE）。强制ISR_SS=1, ISR_NMO=0, ISR_INL=0. 异常中断和不可屏蔽中断不受GIE或INL的控制，自动触发：
| 中断号 | 异常类型 | 名称/缩写 | 写入`XAR`值的含义 |
|---|---|---|---|
| `0x00` | 除数为0 | #DIV | 触发该异常时的P值（模拟器中为触发指令的起始地址） |
| `0x01` | 非法指令 | #II | 同上 |
| `0x02` | 栈异常，显式修改S或F越界，或是栈溢出 | #STACK | 最高位：0=上溢，1=下溢；低31位：导致溢出的S新值 |
| `0x03` | 一般保护异常，非S或F的指针越界、跳转目标非法、用户态执行特权指令、软件中断被拒（嵌套规则或越权） | #GP | 触发该异常时的P值（模拟器中为触发指令的起始地址） |
| `0xFF` | 不可屏蔽中断NMI | #NMI | 不适用 |

`0x00`-`0x0F`与`0xF0`-`0xFF`的中断是保留的，除了特定用途（例如处理异常、陷入内核等），不要随意添加硬中断。

ICT的结构：ICT一共有256项，编号0~255。每一条都有8字节，定义如下：
| 偏移（从低到高） | 内容 | 缩写 |
|---|---|---|
| Byte 0~3 | 该表项的中断处理程序(ISR)基址 | ISR_BASE |
| Byte 4 bit 7 | 该表项（作为软中断时）的特权级。如果越权调用ISR，触发#GP | ISR_DPL |
| Byte 4 bit 6 | 该中断是否允许中断嵌套。0=不允许，1=允许 | ISR_INL |
| Byte 4 bit 5 | 在触发该中断时，是否需要自动暂存S,并将S切换为KSP，在IRET后恢复S。（顺序：在*S处压入RIN3和P后，在*KSP+0处写入旧的S。然后切换S到KSP。在IRET前，先弹出*KSP+0处的S，然后返回到*S来弹出P和RIN3）0不需要1需要 | ISR_SS |
| Byte 4 bit 4 | 在触发该中断时，是否**不**需要自动关闭MPU。0需要1不需要 | ISR_NMO |
| Byte 4 bit 3-0 | 该中断的优先级。如果在处理该中断A时产生了中断B，且B的优先级比A更高（数值上B严格小于A）；且A的ISR_INL=1，那么允许B抢占A | ISR_IPL |
| Byte 5-7 | 保留 | Rsvd |

## 指令编码格式

每条指令由 **首字节** + **第二字节** + **第三字节（操作数表）** + **可选立即数** 构成。对于SR命令，除了Byte 0、Byte 1、Byte 2外，Byte 3为比例系数k，Byte 4为立即数。

### Byte 0 — 控制字节

| 位 | 含义 |
|---|---|
| bit 7 | `REP` 前缀标志 (1=带 REP) |
| bit 6–5 | 操作数尺寸：`00`=不指定，`01`=BYTE，`10`=WORD，`11`=DWORD |
| bit 4 | `NZ` 后缀标志 (1=带 NZ，高位保留 0=不带NZ，高位保留，或不适用NZ) |
| bit 3–0 | 该指令还需要读取的长度（按字节计）：`0000`=2B，`0001`=3B，`0010`=4B，`0011`=5B … 以此类推 |

**REP 语义**：带 REP 前缀的指令以 C 为计数器重复执行：`while (C != 0) { 执行指令; C--; }`。
重复过程不检查/派发中断。注意不要与修改 C 的指令（`CSI`/`CDI`/`TEST`/`CMP`）组合，否则可能不终止。

**请注意：适用NZ标志的命令只包含：`LET`、`LR`、`ADD`、`SUB`、`MNE`、`POP`、`IN`、`XOR`、`AND`、`OR`、`NEG`**

### Byte 1 — 操作码

| 位 | 含义 |
|---|---|
| bit 7 | 一般情况下保留 |
| bit 6–0 | 操作码 |

### Byte 2 — 操作数表

| 位 | 含义 |
|---|---|
| bit 7–4 | 操作数1 (见下方操作数编码表) |
| bit 3–0 | 操作数2 (见下方操作数编码表) |

### 操作数编码表 (Byte 2 中的半字节)

| 编码 | 操作数 | 编码 | 操作数 |
|---|---|---|---|
| 0x0 | `A` | 0x8 | `E` |
| 0x1 | `B` | 0x9 | `R` |
| 0x2 | `C` | 0xA | `X` |
| 0x3 | `D1` | 0xB | `I` |
| 0x4 | `D2` | 0xC | 保留 |
| 0x5 | `S` | 0xD | 保留 |
| 0x6 | `T` | 0xE | 无(用于在立即数或寄存器操作数可选的指令中) |
| 0x7 | `F` | 0xF | 立即数 |

如果操作数为“*DPR”或“*E”，它们的编码与“DPR”、“E”相同。但是在执行时会有解引用操作。

> **立即数**：当操作数编码为 `0xF` 时，立即数从 **第 4 字节** 开始存放（Little‑Endian），其宽度由 Byte 0 的 bit 6–5 决定（BYTE=1B, WORD=2B, DWORD=4B）。  
> **`LR`/`ST` 的指针偏移 `*reg+N`**：偏移量作为立即数紧跟在 Byte 2 之后，宽度由尺寸决定，并按尺寸**符号扩展**（BYTE: -128..127，WORD: -32768..32767，DWORD: 全 32 位范围），因此 `*R-7` 等负偏移可直接书写。  
> **无操作数指令**：`CSI`、`CDI`、`JMP`、`PUSHR`、`POPR`、`SRA`、`SRB`、`DIV QWORD` 以及 `PUSH/POP RIN*` 系列等等，只需要 Byte 0 和 Byte 1 ，不允许填充 Byte 2 。  
> **尺寸显式指定**：所有涉及内存或 I/O 的指令（如 `LR`、`ST`、`PUSH`、`POP`、`IN`、`OUT`、`BLKS`、`SHL`、`SHR` 等）**必须**在汇编语法中带有 `BYTE/WORD/DWORD`，该尺寸编码在 Byte 0 bit 6–5 中。

## 指令集参考

操作码从 **0x00** 到 **0x44** 连续排列，共 **69** 条指令。  

操作数约定：  

`[DPR]` = S, T, A, B, F, R, I

`[DR]` = A, B, C, D1, D2, R, X, I

`[CPR]` = P, E  

`*DPR` 表示指针所指向的内存单元。  

`需要尺寸` 表示该指令的汇编语法 **必须** 包含 `BYTE/WORD/DWORD`。

`'DPR'或'DR'、'CPR'、'N'`表示这个操作数是可选的。

### 数据传送 (0x00–0x05)

| # | 指令 | 格式 | 语义 | 操作码 |
|---|---|---|---|---|
| 0 | `LET` *需要尺寸* | `LET [DPR/DR/E], [BYTE/WORD/DWORD] N` | `reg = N` | `0x00` |
| 1 | `MOV` | `MOV [DPR1/DR1/E1], [DPR2/DR2/E2]` | `reg1 = reg2` | `0x01` |
| 2 | `XCHG` | `XCHG [DPR1/DR1/E1], [DPR2/DR2/E2]` | 交换两寄存器值 | `0x02` |
| 3 | `LR` *需要尺寸* | `LR [DPR/DR], [BYTE/WORD/DWORD] *DPR/*E'+N'` | 加载内存到寄存器 | `0x03` |
| 4 | `ST` *需要尺寸* | `ST [BYTE/WORD/DWORD] *DPR/*E'+N', [DPR/DR]` | 存储寄存器到内存 | `0x04` |
| 5 | `ZERO` | `ZERO [DPR/DR/E]` | 寄存器清零 | `0x05` |

### 四则运算 (0x06–0x0C)

| # | 指令 | 格式 | 语义 | 操作码 |
|---|---|---|---|---|
| 6 | `ADD` *需要尺寸* | `ADD [BYTE/WORD/DWORD] [DPR1/DR1], [DPR2/DR2/N]` | `[DPR1/DR1] = [DPR1/DR1] + [DPR2/DR2/N]` | `0x06` |
| 7 | `SUB` *需要尺寸* | `SUB [BYTE/WORD/DWORD] [DPR1/DR1], [DPR2/DR2/N]` | `[DPR1/DR1] = [DPR1/DR1] - [DPR2/DR2/N]` | `0x07` |
| 8 | `MUL` *需要尺寸* | `MUL [BYTE/WORD/DWORD] [DPR1/DR1], [DPR2/DR2]` | BYTE → D2[15:0] ; WORD → D2 ; DWORD → D1:D2 | `0x08` |
| 9 | `DIV` *需要尺寸* | `DIV [BYTE/WORD/DWORD] [DPR1/DR1], [DPR2/DR2]` | D2=商, D1=余数 (尺寸同) | `0x09` |
| 10 | `DIV QWORD` | `DIV QWORD` | D1:D2 / A:B → D2=商, D1=余数 | `0x0A` |
| 11 | `CSI` | `CSI` | (C Step) C自增 | `0x0B` |
| 12 | `CDI` | `CDI` | (C Decrease) C自减 | `0x0C` |

### 位运算 (0x0D–0x15)

| # | 指令 | 格式 | 语义 | 操作码 |
|---|---|---|---|---|
| 13 | `SHL` *需要尺寸* | `SHL [BYTE/WORD/DWORD] [DPR/DR], N` | 逻辑左移 N 位 | `0x0D` |
| 14 | `SHR` *需要尺寸* | `SHR [BYTE/WORD/DWORD] [DPR/DR], N` | 逻辑右移 N 位 | `0x0E` |
| 15 | `MSL` *需要尺寸* | `MSL [BYTE/WORD/DWORD] [DPR/DR], N` | 算术左移 N 位 | `0x0F` |
| 16 | `MSR` *需要尺寸* | `MSR [BYTE/WORD/DWORD] [DPR/DR], N` | 算术右移 N 位 | `0x10` |
| 17 | `AND` *需要尺寸* | `AND [BYTE/WORD/DWORD] [DPR1/DR1], [DPR2/DR2]` | 按位与，高位保留，结果存入第一个操作数 | `0x11` |
| 18 | `OR` *需要尺寸* | `OR [BYTE/WORD/DWORD] [DPR1/DR1], [DPR2/DR2]` | 按位或，高位保留，结果存入第一个操作数 | `0x12` |
| 19 | `XOR` *需要尺寸* | `XOR [BYTE/WORD/DWORD] [DPR1/DR1], [DPR2/DR2]` | 按位异或，结果存入第一个操作数 | `0x13` |
| 20 | `NEG` | `NEG [DPR/DR]` | 按位取反 | `0x14` |
| 21 | `MNE` *需要尺寸* | `MNE [BYTE/WORD/DWORD] [DPR/DR]` | (Mathematical NEG) 算术取反 | `0x15` |

### 通用的栈操作 (0x16–0x19)

| # | 指令 | 格式 | 语义 | 操作码 |
|---|---|---|---|---|
| 22 | `PUSH` *需要尺寸* | `PUSH [BYTE/WORD/DWORD] [DPR/DR/E/N]` | `S++; *S = value` | `0x16` |
| 23 | `POP` *需要尺寸* | `POP [BYTE/WORD/DWORD] [DPR/DR/E]` | `dst = *S; S--` | `0x17` |
| 24 | `SFA` | `SFA [BYTE/DWORD/WORD] N` | (Set Frame for Allocation) F=S; S+=N; 函数入口处，建立栈帧，为局部变量分配空间。N为局部变量所占用的字节数 | `0x18` |
| 25 | `RER` | `RER` | (Reset codE for Returning) S=F; POP E; 函数返回前，释放局部变量空间，为接下来的跳转做准备 | `0x19` |

### 利用R的地址计算 (0x1A–0x20) 

| # | 指令 | 格式 | 语义 | 操作码 |
|---|---|---|---|---|
| 26 | `PUSHR` | `PUSHR` | `PUSH DWORD R` | `0x1A` |
| 27 | `POPR` | `POPR` | `POP DWORD R` | `0x1B` |
| 28 | `SRA` | `SRA` | `R = A` | `0x1C` |
| 29 | `SRB` | `SRB` | `R = B` | `0x1D` |
| 30 | `LOD` | `LOD [BYTE/WORD/DWORD] [DR]` | 加载并递增 `[DR]=*R, R+=尺寸(BYTE/WORD/DWORD)` | `0x1E` |
| 31 | `STO` | `STO [BYTE/WORD/DWORD] [DR]` | 保存并递增 `*R=[DR], R+=尺寸(BYTE/WORD/DWORD)` | `0x1F` |
| 32 | `SR` | `SR [BYTE/WORD/DWORD] '[DPR1/E1]' '+[DPR2/E2/N2]*k' '+N3'` | `R = 基址[DPR1/E1] + [DPR2/E2/N2]*k + 偏移N3`，这三个可选操作数必须至少有一个不为空。如果[DPR/E2/N2]为立即数，那么汇编器会自动计算N2*k,将其和N3合并。(k为2的幂次，这里的尺寸决定的是立即数的尺寸) | `0x20` |

### 跳转 (0x21–0x2F)

所有跳转都将 `P` 设置为 `E` 的当前值。  
`C` 用作比较寄存器。条件跳转比较 `C` 与操作数 (低 N 位)。

| # | 指令 | 格式 | 条件 | 操作码 |
|---|---|---|---|---|
| 33 | `TEST` *需要尺寸* | `TEST [BYTE/WORD/DWORD] [DPR/DR]` | 将C的低BYTE/WORD/DWORD与DPR/DR的低～按位与，保存在C中 | `0x21` |
| 34 | `CMP` *需要尺寸* | `CMP [BYTE/WORD/DWORD] [DPR/DR]` | 将C的低～与DPR/DR的低～相减，保存在C中 | `0x22` |
| 35 | `JMP` | `JMP` | 无条件 | `0x23` |
| 36 | `JZ` | `JZ` | `C == 0` | `0x24` |
| 37 | `JNZ` | `JNZ` | `C != 0` | `0x25` |
| 38 | `JRZ` *需要尺寸* | `JRZ [BYTE/WORD/DWORD] [DPR/DR]` | `reg 低N位 == 0` | `0x26` |
| 39 | `JRNZ` *需要尺寸* | `JRNZ [BYTE/WORD/DWORD] [DPR/DR]` | `reg 低N位 != 0` | `0x27` |
| 40 | `JA` *需要尺寸* | `JA [BYTE/WORD/DWORD] [DPR/DR]` | 无符号 `C > reg` | `0x28` |
| 41 | `JNA` *需要尺寸* | `JNA [BYTE/WORD/DWORD] [DPR/DR]` | 无符号 `C ≤ reg` | `0x29` |
| 42 | `JB` *需要尺寸* | `JB [BYTE/WORD/DWORD] [DPR/DR]` | 无符号 `C < reg` | `0x2A` |
| 43 | `JNB` *需要尺寸* | `JNB [BYTE/WORD/DWORD] [DPR/DR]` | 无符号 `C ≥ reg` | `0x2B` |
| 44 | `JG` *需要尺寸* | `JG [BYTE/WORD/DWORD] [DPR/DR]` | 有符号 `C > reg` | `0x2C` |
| 45 | `JNG` *需要尺寸* | `JNG [BYTE/WORD/DWORD] [DPR/DR]` | 有符号 `C ≤ reg` | `0x2D` |
| 46 | `JL` *需要尺寸* | `JL [BYTE/WORD/DWORD] [DPR/DR]` | 有符号 `C < reg` | `0x2E` |
| 47 | `JNL` *需要尺寸* | `JNL [BYTE/WORD/DWORD] [DPR/DR]` | 有符号 `C ≥ reg` | `0x2F` |

### I/O与中断处理 (0x30–0x39)

**推荐的RINx寄存器出入栈顺序：**
```
PUSHI
PUSH RIN1
PUSH RIN2
...
POP RIN2
POP RIN1
POPI
```

在对RIN系列寄存器操作时，中断自动被屏蔽。

| # | 指令 | 格式 | 语义 | 操作码 |
|---|---|---|---|---|
| 48 | `IN` *需要尺寸* | `IN [BYTE/WORD/DWORD] [DPR1/DR1], [DPR2/DR2]` | 从端口 `DPR2/DR2` 读入到 `DPR1/DR1` | `0x30` |
| 49 | `OUT` *需要尺寸* | `OUT [BYTE/WORD/DWORD] [DPR1/DR1], [DPR2/DR2] ` | 将 `DPR2/DR2` 的低N位写入端口 `DPR1/DR1` | `0x31` |
| 50 | `INT` | `INT [N/DR]` | 触发软件中断 N（这里N必须是一个BYTE），自动压栈RIN3和P，查表跳转 | `0x32` |
| 51 | `PUSH RIN1` | `PUSH RIN1` | 压入 RIN1 (BYTE) | `0x33` |
| 52 | `PUSH RIN2` | `PUSH RIN2` | 压入 RIN2 (BYTE) | `0x34` |
| 53 | `POP RIN1` | `POP RIN1` | 弹出到 RIN1 | `0x35` |
| 54 | `POP RIN2` | `POP RIN2` | 弹出到 RIN2 | `0x36` |
| 55 | `PUSHI` | `PUSHI` | (PUSH RIN3 and disable Interrupts) 先压RIN3高32位，再压RIN3低32位，并屏蔽中断 | `0x37` |
| 56 | `POPI` | `POPI` | (POP RIN3 and enable Interrupts) 先弹RIN3低32位，再弹RIN3高32位，并开启中断 | `0x38` |
| 57 | `HLT` | `HLT` | 停机等待中断 | `0x39` |

### 批量操作及其他操作 (0x3A–0x3E)

| # | 指令 | 格式 | 语义 | 操作码 |
|---|---|---|---|---|
| 58 | `BLKS` *需要尺寸* | `BLKS [BYTE/WORD/DWORD] [DR/N]` | 批量赋值：将R所指向的值往后C个内存BYTE/WORD/DWORD的值全部赋值为DR/N | `0x3A` |
| 59 | `PUSH P` *需要尺寸* | `PUSH [BYTE/WORD/DWORD] P` | 将P寄存器的低BYTE/WORD/DWORD压栈 | `0x3B` |
| 60 | `NOP` | `NOP` | 空指令 | `0x3C` |
| 61 | `INC` | `INC [DPR/DR/E]` | 简单自增 | `0x3D` |
| 62 | `DEC` | `DEC [DPR/DR/E]` | 简单自减 | `0X3E` |
| 63 | `BLKIN` *需要尺寸* | `BLKIN [BYTE/WORD/DWORD] [DPR/E]` | 读取A号端口，连续(DWORD)C次，写入DPR/E所指向的内存 | `0x3F` |

### 内存保护指令 (0x40-0x43)

**此处的SYSREG操作数也位于Byte 2半字节。它们的定义如下：**
| 编码 | 系统寄存器 | 说明 |
|---|---|---|
| 0x0 | CBASE | 代码基址 |
| 0x1 | CLIMIT | 代码上限 |
| 0x2 | DBASE | 数据基址 |
| 0x3 | DLIMIT | 数据上限 |
| 0x4 | KSP | 内核栈指针，指向栈底。切换任务时调度器会修改它。 |
| 0x5 | RIN3_CTRL(RIN3的低32位（系统控制寄存器）) | / |
| 0x6 | XAR | 异常信息 |
| 0x7 | ICTB | RIN3的高32位，中断控制表(ICT)的代码基址 |
| 0x8-0xF | 保留 | / |

**对于下述指令，MPU会检查内存读写是否越界（MPU=1 时生效，越界触发 #GP；栈操作越界触发 #STACK）：**
| 指令 | 操作码 | 何时检查 | 检查对象 |
|---|---|---|---|
| `LR` | `0x03` | 读取*A, *B, *T, *F, *S, *R，*I时 | 指针地址+尺寸(BYTE/WORD/DWORD) 必须落在范围[DBASE, DLIMIT) |
| `ST` | `0x04` | 写入*A, *B, *T, *F, *S, *R, *I时 | 同上 |
| `LOD` | `0x1E` | 读取*R时 | 检查R地址+尺寸，和R+尺寸，是否越界 |
| `STO` | `0x1F` | 写入*R时 | 同上 |
| `PUSH` | `0x16` | 压栈时 | 检查S+尺寸是否<DLIMIT（越界→#STACK） |
| `POP` | `0x17` | 弹栈时 | 检查S-尺寸是否>=DBASE（越界→#STACK） |
| `BLKS` | `0x3A` | 批量赋值时 | 遍历检查[R, R+C*尺寸] 是否越界 |
| `BLKIN` | `0x3F` | 写入内存时 | 同上 |
| `POR` | `0x44` | 写入*DPR/*E时 | *DPR→检查[DBASE, DLIMIT)；*E→检查[CBASE, CLIMIT) |

**对于下述指令，MPU会检查内存指针是否越界（写E越界→#GP；写S/F越界→#STACK）：**
| 指令 | 操作码 | 何时检查 | 检查对象 |
|---|---|---|---|
| `LET` | `0x00` | 向E, S, F写入立即数时 | 若目标为E，检查[CBASE, CLIMIT)；若目标为S/F，检查[DBASE, DLIMIT) |
| `MOV` | `0x01` | 向E, S, F搬运数据时 | 同上 |
| `POP` | `0x17` | 弹栈到E, S, F时 | 检查新值是否在区间内 |
| `SFA` | `0x18` | 修改S时 | 检查S是否在区间内（越界→#STACK） |
| `RER` | `0x19` | 修改S时 | 检查S是否在区间内（越界→#STACK） |
| `JMP`等跳转指令 | `0x23`-`0x2F` | 隐式写入P之前 | 检查E的值是否在[CBASE, CLIMIT) |

| # | 指令 | 格式 | 语义 | 操作码 |
|---|---|---|---|---|
| 64 | `SVC` | `SVC` | 陷入内核。硬件将RIN3的CPL置为0，GIE置为0，且护栏因进入内核态而暂时失效。调用中断`0xFE`。（实现注：中断帧压入的是**原始**RIN3，因此IRET恢复到用户态原状——CPL=1、GIE恢复；进入ISR后的工作状态才是CPL=0/GIE=0） | `0x40` |
| 65 | `IRET` | `IRET` | 返回用户态。硬件弹栈，恢复`RIN3`的完整值。自动按照恢复后的值工作。 | `0x41` |
| 66 | `SETB` | `SETB [SYSREG], [DR]` | 写系统寄存器。将[DR]的值写入系统寄存器[SYSREG]，只有内核态允许执行。 | `0x42` |
| 67 | `GETB` | `GETB [DR], [SYSREG]` | 读系统寄存器。将[SYSREG]的值写入寄存器[DR]，高位自动补零，只有内核态允许执行。 | `0x43` |

### DOCTOR 3.3新增的指令 (0x44)

| # | 指令 | 格式 | 语义 | 操作码 |
|---|---|---|---|---|
| 68 | `POR` *需要尺寸* | `POR [BYTE/WORD/DWORD] [*DPR/*E]` | (POP to RAM) 将栈顶数据弹出到*DPR或*E所指定的内存空间中 | `0x44` |

## 机器码编码示例

### 示例 1：LET A, DWORD 0x12345678

```
Byte 0: 0x65  (01100101B, 无REP，无NZ，数据需要5Byte)
Byte 1: 0x00  (LET)
Byte 2: 0x0F  (A, 立即数)
Byte 3: 0x78  (Little Endian)
Byte 4: 0x56
Byte 5: 0x34
Byte 6: 0x12
```

### 示例 2：ADD DWORD A, B

```
Byte 0: 0x61  (尺寸=DWORD=11, 长度=3字节 → 长度字段=0001)
Byte 1: 0x07  (操作码 ADD)
Byte 2: 0x01  (操作数1=A, 操作数2=B)
```

### 示例 3：SUBNZ D1, BYTE 0x10

```
Byte 0: 0x32 (00110010B，无REP,有NZ,BYTE,还需要读取2Byte)
Byte 1: 0x07 (SUB)
Byte 2: 0x3F (D1, 立即数)
Byte 3: 0x10 (立即数)
```

### 示例 4：GETB D1, KSP

```
Byte 0: 0x01 (0000_0001B，无REP,不适用尺寸，无NZ,还需要读取1Byte)
Byte 1: 0x43 (GETB)
Byte 2: 0x34 (D1, KSP，其中KSP是SYSREG，编号特殊定义)
```

### 示例 5：CSI

```
Byte 0: 0x00 (0000_0000B，无REP，不适用尺寸，无NZ，还需要读取0Byte)
Byte 1: 0x0B (CSI)
```

### 示例 6：SR BYTE T+X*8+0x02

```
Byte 0: 0x06 (0010_0011B，无REP，BYTE，无NZ，还需要读取3Byte)
Byte 1: 0x20 (SR)
Byte 2: 0x6A (T, X)
Byte 3: 0x03 (k=8=2^3,这里指定的是2的幂次，此处为3)
Byte 4: 0x02 (被指定为尺寸BYTE的立即数)

## 设计哲学

- **工具而非规则**：提供最基本的工具，允许自由组合，而非规则限制。
- **栈即一切**：许多操作都可以直接用栈访问。

