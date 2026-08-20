# doctor-emu - DOCTOR 的模拟器

## TODO
- [x] 完善MPU功能（内存护栏：CBASE/CLIMIT/DBASE/DLIMIT 越界检查 → #GP/#STACK）
- [x] 接入异常体系（#DIV/#II/#STACK/#GP/#NMI 触发与派发、XAR、SVC 特权级切换）
- [x] 实现基础 69 条指令（0x00–0x44）及 DFE/DDE 浮点扩展（0x45–0x66）
- [x] 完善 Display 层（后端抽象：tty/ansi/ppm/fb，FB 设备已接入）
- [x] 添加 UART 设备（环回/状态/溢出/接收中断 IRQ4=COM1，输出走 Display 层）
- [x] 添加 AT 兼容键盘 KBC（Set 1 扫描码、8042 命令/状态、电平式中断 IRQ1）
- [x] 增加 DISK 设备（端口 I/O 块设备，见 `dev_specification.md`）
- [x] 增加 RTC 设备（端口 `0x30`-`0x3D`，IRQ2，见 `dev_specification.md`）
- [ ] 完善 `manual.md` 与 `dev_specification.md`
- [x] 提供独立反汇编器 `dda`；dasm 内置反汇编仍为可选的后续增强

## 编译方法
推荐使用GCC编译器。在命令行中输入：
```bash
make
```
即可编译。输出的文件位于build/内。

## 运行
```bash
# 默认加载当前目录的 code.bin 与 data.bin
./build/bin/doctor-sim

# 指定镜像文件：指定了某个子参数时，未提及的文件不会被加载
./build/bin/doctor-sim -f code <code.bin>          # 只加载代码
./build/bin/doctor-sim -f data <data.bin>          # 只加载数据
./build/bin/doctor-sim -f code <code.bin> data <data.bin>   # 两者都加载
./build/bin/doctor-sim -d <disk.raw> ...           # 加载 DISK 块设备镜像（raw）

# 帮助 / 版本
./build/bin/doctor-sim -h
./build/bin/doctor-sim -v

# Display 层（后端抽象）：模拟器/设备只发显示请求，由后端决定显示方式
./build/bin/doctor-sim --display tty ...     # 字符输出到终端（默认）
./build/bin/doctor-sim --display ansi ...    # ANSI 颜色 + 帧缓冲字符画
./build/bin/doctor-sim --display ppm --display-file out.ppm --display-size 320x200 ...
                                             # 帧缓冲导出 PPM 图像
./build/bin/doctor-sim --display fb ...      # 帧缓冲保留内存（程序化消费）

# 宿主键盘输入（stdin 为终端时自动启用，转发为 KBC 的 Set 1 扫描码）
#   安全键: 第一次 Ctrl+C 暂停模拟（输入不再转发）；再次 Ctrl+C 恢复。
#   暂停时支持命令:
#     d           显示当前寄存器转储
#     s [num]     执行 num 步；不带 num 时执行 1 步
#     stack       显示当前栈 ±8 BYTE 的内存
#     q           退出
./build/bin/doctor-sim -f code demo_code.bin data demo_data.bin
```

## DOCTOR 汇编器 (dasm)
汇编器位于 `dasm/`（仓库根目录下），用法：
```bash
cd ../dasm && make
./dasm <input.asm> [code.bin] [data.bin]
```

测试程序位于 `tests/`，每个 `.asm` 文件头部注释写明期望结果（通过标记为转储中
`A` 寄存器的特定 magic 值），一键运行：
```bash
sh tests/run_test.sh tests/test_arith.asm     # 运行单个测试（自动断言 PASS/FAIL）
sh tests/run_irq_test.sh                      # PIT→IRQ0→ISR→IRET 端到端测试
sh tests/run_dasm_test.sh                     # dasm 语法修复的字节级测试（22项）
sh tests/run_display_test.sh                  # Display 层 + FB 设备（PPM 导出断言）

演示程序（交互式，非自动化测试）:
```bash
../dasm/dasm tests/demo_echo.asm demo_code.bin demo_data.bin
./build/bin/doctor-sim -f code demo_code.bin data demo_data.bin
# 终端打字 → 每个按键经 KBC 中断直接回显字符本身（Set 1 扫描码翻译，
#   支持 Shift 大小写/上档、Enter 换行、Backspace 退格、Tab 制表）
# Ctrl+C 暂停/恢复；暂停时支持 d / s [num] / stack / q
```
另：`tests/test_dasm_features.asm` 端到端覆盖 dasm 新语法
（`*reg+N` 偏移 / `PUSH P` / `pushp` / NZ 拼接 / 表达式）与解码器偏移支持；
`tests/test_exception.asm` 覆盖异常体系（#DIV/#II/#STACK/#GP 六种触发 + XAR 验证）；
`tests/test_svc.asm` 覆盖 SVC 特权级切换与内核态检查；
`tests/test_nz_neg_mne.asm` 覆盖 NEG/MNE 的 NZ 变体；
`tests/test_mpu.asm` 覆盖 MPU 护栏（数据/代码区间越界 → #GP/#STACK）；
`tests/test_int_gie.asm` 覆盖软中断在 GIE=0 时仍可触发；
`tests/test_fb.asm` 覆盖 FB 帧缓冲设备与 Display 层（配合 `run_display_test.sh`）；
`tests/test_uart.asm` 覆盖 UART 环回/状态/溢出/接收中断（IRQ4→ISR）；
`tests/test_kbc.asm` 覆盖 KBC 自测/命令字节/键盘命令/OBF/中断（IRQ1→ISR）；
`tests/test_mem_fault.asm` 覆盖数据内存物理边界（越界 → #GP/#STACK）。

## 已知问题与限制

### A. 模拟器 (src/) —— 待修改

#### A1. 未实现的指令
| 指令 | 状态 | 说明 |
|---|---|---|
| 0x00–0x44（基础 69 条） | **全部实现** | 含 SVC(0x40)、MNE/NEG 的 NZ 变体 |
| 0x45–0x54（DFE） | **全部实现** | 单精度浮点扩展 |
| 0x55–0x66（DDE） | **全部实现** | 双精度浮点扩展 |

#### A2. 中断 / 异常 / 特权级
- **异常体系已接入**：`raise_exception()` 触发 #DIV(0x00)/#II(0x01)/#STACK(0x02)/#GP(0x03)/
  #NMI(0xFF)，写入 XAR 并派发；异常/NMI 不可屏蔽、强制 ISR_SS=1/ISR_NMO=0/ISR_INL=0。
  触发源：除零→#DIV；非法指令（decode/execute 错误）→#II；栈上溢/下溢、SFA/RER 修改 S
  越界→#STACK；用户态执行 SETB/GETB、软件中断被拒、跳转目标越界、中断越权→#GP。
  原「错误计数 + DOUBLE ERR 停机」已移除；异常派发失败（如栈上溢）→ FATAL（双重故障），
  连续异常超过 6 次 → FATAL（异常风暴防护）。
- **XAR 语义**：除 #STACK 外，XAR = 触发异常的指令起始地址（manual 的「P 值」按此解释，
  已同步 manual.md）；#STACK 的 XAR = bit31(0=上溢/1=下溢) | 低31位(导致溢出的 S 新值)。
- **内核态检查已实现**：`SETB`/`GETB` 仅 CPL=0 允许，用户态执行 → #GP。
- **SVC 已实现**：CPL→0、GIE→0、护栏（MPU）失效，调用中断 0xFE。压入**原始** RIN3，
  IRET 恢复用户态原状（CPL=1、GIE 恢复）。
- **软中断不受全局中断开关影响**（manual 已同步）：`INT` 在 GIE=0 或 PUSHI/POPI 内部
  也总是尝试派发（不 #GP）；仅嵌套规则拒绝或越权（DPL）时触发 #GP。
- **嵌套中断**：INL/IPL 抢占规则已修正；异常/NMI 的不可屏蔽路径已有测试覆盖
  （test_exception.asm 连续 6 种异常）。
- ~~IRET 每次执行调用 `exe_err()`~~ **已移除**（调试噪音）。

#### A3. MPU 内存保护（已实现）
- CTRL bit29 (MPU)=1 时护栏开启（复位默认关闭）；异常/NMI/SVC 处理期间强制关闭（ISR_NMO=0）。
- **内存读写越界检查**（越界 → #GP；栈操作越界 → #STACK）：
  - `LR`/`ST` 的数据指针解引用（*A..*I，*E 除外）→ `[DBASE, DLIMIT)`
  - `LOD`/`STO` 的 *R、`BLKS`/`BLKIN` 的批量区间、`POR` 的写入目标（*E → 代码区间）
  - `PUSH`/`POP`/`PUSHR`/`POPR`/`PUSH RINx`/`POP RINx`/`PUSHI`/`POPI`/`PUSH_P` 的栈区间
- **内存指针越界检查**（写 E 越界 → #GP；写 S/F 越界 → #STACK）：
  - `LET`/`MOV`/`POP` 写 E 时 → `[CBASE, CLIMIT)`；写 S/F 时 → `[DBASE, DLIMIT)`
  - `SFA`/`RER` 修改 S → `[DBASE, DLIMIT)`
  - `JMP` 等跳转目标 E → `[CBASE, CLIMIT)`
- #STACK 的 XAR 编码与异常体系一致（bit31: 0=上溢/1=下溢；低31位=S 新值）。
- 实现注：MPU 开启时 `LET`/`MOV` 无法写入越界 E/S/F（写入即被拒），因此跳转目标越界
  只能通过先关 MPU 注入、再开启后 JMP 触发（见 test_mpu.asm 第 9 段）。

#### A4. 设备层
- `device_tick_all()` 每指令调用一次（`cycles=1`），**无真实周期计数**，设备 tick 粒度粗。
- ~~`device_destroy_all` 从未调用 / 设备私有数据泄漏~~ **已修复**：`cpu_free()` 调用
  `device_destroy_all` 释放 PIT/FB/UART 私有数据（ASan detect_leaks 验证无泄漏）；
  `device_dump_all` 由 `--dump-devices` 参数触发；`device_reset_all` 仍为预留接口。
- 已实现 **PIT**、**FB**（帧缓冲）、**UART**、**KBC**（AT 兼容键盘）与 **DISK**（块设备），
  端口与行为见 `dev_specification.md`；RTC 也已实现。
- **MMIO 未接入**：`device.h` 有 `is_mmio`/`base_mem` 字段，但内存访问路径
  （`mem.h` 的 load/store）没有设备映射逻辑。
- PIT 细节：真实时间模式（μs/ms/s，TU=00/01/10）用墙钟驱动，与模拟执行速度无关，
  测试不可复现；`pit_read_port` 忽略 `size` 参数（恒返回 32 位）。

#### A5. CPU 核心实现问题
- ~~`decode.c` 文件级静态变量 `P`~~ **已消除**：改为局部变量 + 显式取指游标，decode 可重入。
- ~~取指无边界检查~~ **已修复**：`decode.c` 逐字节检查 `CODE_SIZE` 边界，越界返回 -1（#II），
  不再依赖宿主 SIGSEGV 兜底。
- ~~数据内存访问无边界检查~~ **已修复**：`mem.h` 的 load/store 全部内置物理边界防护
  （越界返回 0 / 忽略写入，不再访问宿主内存）；指令级（LR/ST/LOD/STO/BLKS/BLKIN/POR/
  LET-MOV-POP 写 E/S/F、JMP 目标、栈操作）在访问前检查物理范围 → #GP/#STACK，
  与 MPU 区间检查统一在 `mem_check_data/code`（物理边界总是检查，MPU=1 时叠加区间）。
  `main.c` 的 SIGSEGV + siglongjmp 保留为最后一层兜底。
- ~~`REP` 前缀仅 `XCHG` 实现~~ **已改为统一语义**：REP 在 `cpu_run()` 中实现
  （`while (C != 0) { 执行; C--; }`），废弃了 execution.c 里 REP XCHG 的奇偶特判。
  注意：REP 重复过程不 tick 设备、不派发中断；与 `CSI`/`CDI`/`TEST`/`CMP` 组合可能不终止。
- ~~`SHL`/`SHR`/`MSL`/`MSR` 移位数 `& 0x1f` 截断~~ **已修复**：移位数与结果均按尺寸截断
  （BYTE: 0x07/0xFF，WORD: 0x0F/0xFFFF，DWORD: 0x1F/全32位）；`MSR` 按尺寸取符号位并扩展。
- ~~`DIV QWORD` 调试打印~~ **已移除**。
- ~~`main.c` 遗留的无用 `decode(&cpu, &instr)` 调用~~ **已移除**。
- **NZ 高位保留语义统一修复**：`ADD`/`SUB`/`LET`/`MOV`/`POP` 的 NZ 变体此前会清零高位
  （ADD NZ 只留低字节）或存在进位污染高位（LET/MOV/POP 用 `+=`）；已统一为
  「高位保留、低 N 位替换/运算」并与 AND/OR/XOR/IN 的 NZ 行为一致。
- **有符号条件跳转比较修复**：`JG`/`JNG`/`JL`/`JNL` 的比较表达式
  `(int32_t)C & mask` 中 `& mask` 会把结果提升为无符号，导致「C 为负、reg 为 0」
  时（如 C=0xFFFFFFF6 与 X=0 比较，即 -10 < 0）被错误判为 false；已改为
  `(int32_t)(C & mask) < (int32_t)res`（先截断、再按 int32 解释）。
  修复前 `-10 < 0` 不成立（被 dcc 生成的 `i < n` 循环暴露）；修复后
  `tests/test_jump.asm` 与 dcc 端到端测试全部通过。

#### A6. 构建 / 杂项
- Makefile `debug` 目标添加的 `-DDEBUG` 宏在代码中未使用（`#ifdef DEBUG` 不存在）。
- 版本号 `0.1.0`（`-v` 显示），尚无版本管理/变更日志。
- `README` 中版权年份为 2026。

### B. 汇编器 (dasm/) —— 待修改

#### B1. 语法限制（B1.1–B1.7 已修复，B1.8 待做）
1. ~~`LR`/`ST` 的指针偏移 `*reg+N`~~ **已支持**：`LR DWORD A, *R+4`、`ST BYTE *I-2, B`；
   偏移量按尺寸符号扩展（BYTE/WORD/DWORD），模拟器解码器已同步支持。
2. ~~`PUSH P`（`pushp`）~~ **已支持**：`PUSH [BYTE/WORD/DWORD] P` 与关键字 `pushp`
   （隐含 DWORD），均编码为 0x3B（操作数表填 0xEE）。
3. ~~NZ 后缀只支持分离写法~~ **已支持拼接**：`ADDNZ BYTE A, 0x21` 与 `ADD NZ BYTE A, 0x21` 等价。
4. ~~立即数不支持算术表达式~~ **已支持**：`+`/`-` 二元表达式（如 `LET A, DWORD label+4`、
   `RESB 0x110 - $`）；项可为立即数/标号/`$`。注意：表达式不支持括号、不支持前向标号参与
   尺寸类表达式（如 `RESB` 的数量），前向标号作立即数时 pass1 按 0 处理。
5. ~~`ORG` 只影响符号地址~~ **已修复**：`ORG` 会将输出缓冲填充 0x00 到目标地址；
   `DB/DW/DD/DQ` 的地址参数会把数据定位到指定地址（之前自动补零填充）。地址小于当前偏移时报错。
6. ~~`DB/DW/DD/DQ` 地址参数被忽略~~ 见上一条（已实现固定地址定位）。
7. ~~错误行号为预处理后行号~~ **已修复**：错误报告的 `行 N` 对应源文件真实行号。
8. **dasm 内置反汇编** —— 仓库已有独立反汇编器 `dda`（读取 ELF32 `.o` 输出 DASM）；如需在 dasm 内直接提供二进制反汇编入口，可作为后续增强。

#### B1.9 新增的严格检查（本次加入）
- **尺寸检查**：manual 标注「需要尺寸」的指令（`PUSH`/`POP`/`LR`/`ST`/`ADD`/`SFA` 等）缺少
  `BYTE/WORD/DWORD` 时直接报错，而不是生成运行时会 #II 的坏编码。
- **解引用检查**：`*reg` 仅允许 `LR`（op2）/`ST`（op1）/`POR`（op1），偏移 `+N` 仅 `LR`/`ST`；
  其余指令使用 `*` 报错。
- **`INT N` 缺省 BYTE**：manual 规定 N 是一个 BYTE，未给尺寸时按 BYTE 编码。
- **寄存器 P** 只允许出现在 `PUSH P`，其余指令使用 P 报错。
- **负尺寸/回退地址**：`RESB` 数量为负、`ORG`/数据地址回退均报错。

#### B2. 已修复的历史 bug（记录备查）
- `0x` 前缀的十六进制数以 `b/B` 结尾被误判为二进制数（如 `0xFFFFFFFB`）→ token 流截断。
- sysreg 操作数在 encoder 中被二次编码（`SETB RIN3_CTRL, D1` 的 sysreg 半字节编成 `0xF`）。
- REP 前缀形式不支持（已支持 `REP XCHG`，并兼容后缀 `XCHG REP`）。
- `BLKS`/`BLKIN` 被当作双操作数解析导致无法汇编（已加入单操作数列表）。
- **encoder 的「无操作数表」列表漏了 `RER`**：RER 被编成 3 字节（`01 19 EE`）而
  parser/decode 按 2 字节处理，导致 RER 之后的标号地址错位 1 字节（已修复）。
- **pass2 不重置符号偏移**：`symbol_get_current_address` 在第二遍返回 pass1 结束时的
  偏移，导致 `$` 求值与 ORG 检查错误（新增 `symbol_reset_offsets`，pass2 同步推进偏移）。
- **`PUSH DWORD P` 的 opcode 用了转换前的值**（编成 0x16 而非 0x3B）——opcode 计算移到
  指令类型转换（PUSH P → PUSH_P）之后（已修复）。
- **无操作数指令的操作数结构未初始化**：`parsed_operand`/`imm_expr` 无构造函数，
  局部默认初始化产生垃圾值，导致随机报「不支持 * 解引用」（已改为 `= {}` 值初始化）。

### C. 模拟器已修复的历史 bug（记录备查）
- PIT 真实时间模式 `last_time_ns` 从不更新（`units` 累计的是总耗时，重载后计数器被瞬间清零）。
- PIT TE 使能且计数器为 0 时立即误触发 IRQ（新增 `counter_loaded` 门控）。
- 主循环从不调用 `device_tick_all`、不派发硬件中断；HLT 不等待中断（已接通并实现停机等待）。
- 中断嵌套规则错误（INL=0 时仍允许抢占）；IRET 未把 `current_int=0xFF` 还原为 `-1`。
- `MOV` 无尺寸时报 #II（应视为 DWORD）；`MOVNZ` case 3 误把操作数编码当值写入。
- `BLKS`/`BLKIN` 步长恒为 1 字节（应为 1/2/4，按尺寸步进）。
- `ICTB`（RIN3 高 32 位）无 SETB/GETB 编码（已扩展 SYSREG 0x7，`manual.md` 已同步）。
- `manual.md` DIV QWORD 注释与实现矛盾（已统一为「D2=商, D1=余数」）。
- **`LR`/`ST` 的 `*reg+N` 解码缺失**：操作数编码不是 0xF 时 decoder 认为无立即数而返回 #II
  （已按长度字段读取偏移立即数，并按尺寸符号扩展）。
- **`mem.h pop()` case 3 缺少 `break`**（落空到 default，功能侥幸正确，已补）。
- **`test_logic.asm` 的 `MNE A` 缺尺寸**（manual 规定 MNE 需要尺寸，已改为 `MNE DWORD A`）。
- **SVC 压栈顺序错误**：先清 GIE/CPL 再派发导致压入的是已清除的 RIN3，IRET 无法恢复
  用户态原状（已改为先派发压入原始 RIN3，再置内核工作状态）。
- **栈操作边界与字节数不一致**：`push`/`pop` 的越界预检用尺寸码(1/2/3)而非实际字节数
  (1/2/4)，DWORD 栈操作差 1 字节（已统一按字节数检查；`push` 越界返回 2、`pop` 下溢
  不读取，调用方报 #STACK 并写 XAR）。
- **异常触发源补全**：原 `#II` 只打印 INFO、`#DIV` 直接 `handle_intr`、`#STACK`/`#GP`
  无触发条件、错误只计数（DOUBLE ERR 停机）；现统一经 `raise_exception()` 派发并写 XAR，
  移除 DOUBLE ERR 计数（改为派发失败/异常风暴 FATAL）。
- **软中断受 GIE 影响**：原实现中 `INT` 在 GIE=0 / PUSHI-POPI 内部被拒并触发 #GP；
  按 manual 新语义改为软件中断不受全局中断开关影响、总是尝试派发（仅嵌套拒绝/越权 #GP）。
- **MPU 越界检查缺失**：CBASE/CLIMIT/DBASE/DLIMIT 此前只读写不检查；现按 manual 0x40–0x43
  节实现全部读写/指针/跳转检查（MPU=1 生效，越界 → #GP/#STACK），并修正 manual 中
  BLKS 操作码笔误（0x35→0x3A）与 BLKOUT→BLKIN。

## 关于版权
    Copyright (C) 2026  Hangco(hang-coffee)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
