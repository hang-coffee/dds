;; ============================================================
;; demo_echo.asm - 键盘回显演示（演示型，非自动化测试）
;;
;; 功能: 宿主终端按键 → KBC（Set 1 扫描码, 经 input.c 注入 make+break,
;;       大写/上档字符自动附加 Shift 修饰）→ KBC 中断 (IRQ1→0x11)
;;       → ISR 把扫描码翻译为字符本身, 经 UART 回显（走 Display 层）。
;;       支持: 换行(Enter)、退格(Backspace, 输出 \b 空格 \b)、制表(Tab)、
;;       Shift 大小写、扩展键(0xE0)忽略。
;;
;; 运行:
;;   ./tools/dasm/dasm tests/demo_echo.asm demo_code.bin demo_data.bin
;;   ./build/bin/doctor_sim -f code demo_code.bin data demo_data.bin
;;
;;   在模拟器终端打字, 输入的字符直接回显。Ctrl+C 暂停/恢复; 暂停时 q 退出。
;;
;; 数据布局:
;;   0x2000 MAP        Set 1 make → 小写字符（128 字节, 稀疏表）
;;   0x2100 MAP_SHIFT  Set 1 make → 大写/上档字符
;;   0x2200 STATE:     +0 shift 标志, +1 跳过扩展键标志
;;   0x2300 提示字符串
;; ============================================================

	SECTION DATA
	ORG 0x2000
MAP:
	DB 0x2002, "1234567890"		; 0x02-0x0B
	DB 0x200C, "-="			; 0x0C-0x0D
	DB 0x2010, "qwertyuiop"		; 0x10-0x19
	DB 0x201A, "[]"			; 0x1A-0x1B
	DB 0x201E, "asdfghjkl"		; 0x1E-0x26
	DB 0x2027, 0x3B			; ';' (0x27)  — 注释剥离不识别字符串内分号, 用立即数
	DB 0x2028, 0x27			; '\'' (0x28)
	DB 0x2029, "`"			; 0x29
	DB 0x202B, "\\"			; 0x2B
	DB 0x202C, "zxcvbnm"		; 0x2C-0x32
	DB 0x2033, ",./"		; 0x33-0x35
	DB 0x2039, " "			; 0x39 空格
	ORG 0x2100
MAP_SHIFT:
	DB 0x2102, "!@#$%^&*()"		; 上档数字
	DB 0x210C, "_+"			; 上档 - =
	DB 0x2110, "QWERTYUIOP"		; 大写字母
	DB 0x211A, "{}"
	DB 0x211E, "ASDFGHJKL"
	DB 0x2127, ":\""			; "
	DB 0x2129, "~"
	DB 0x212B, "|"
	DB 0x212C, "ZXCVBNM"
	DB 0x2133, "<>?"
	DB 0x2139, " "
	ORG 0x2200
STATE:
	DB 0x2200, 0x00			; shift 标志
	DB 0x2201, 0x00			; 跳过扩展键标志
	ORG 0x2300
MSG:
	DB 0x2300, "KBD echo: \n"

	SECTION TEXT
	ORG 0

START:
	;; ---- ICT[0x11] @ 0x1088 = ISR_KBC ----
	LET R, DWORD 0x1088
	LET A, DWORD ISR_KBC
	STO DWORD A
	LET A, DWORD 0
	STO BYTE A
	LET R, DWORD 0x1000
	SETB ICTB, R
	LET S, DWORD 0x3000

	;; ---- UART: EN=1（输出走 Display 层）----
	LET A, DWORD 0x00000001
	OUT DWORD 0x18, A

	;; ---- KBC: 命令字节 bit0=1（键盘中断使能）----
	LET A, BYTE 0x60			; 写命令字节
	OUT BYTE 0x1B, A
	LET A, BYTE 0x01
	OUT BYTE 0x1A, A

	;; ---- GIE=1, ISRB=0x10 (IRQ1 → 中断号 0x11) ----
	LET D1, DWORD 0x80100000
	SETB RIN3_CTRL, D1

	;; ---- 输出提示 "KBD echo: " ----
	LET R, DWORD 0x2300
	LET C, DWORD 11
ECHO_MSG:
	LOD BYTE A					; *R → A, R++
	OUT BYTE 0x16, A			; UART 输出
	CDI							; C--
	LET E, DWORD ECHO_MSG
	JNZ							; C != 0 继续

	;; ---- 主循环: HLT 等待键盘中断（KBC 电平式中断唤醒）----
LOOP:
	HLT
	LET E, DWORD LOOP
	JMP

;; ==================== ISR ====================
;; 读 KBC 扫描码 → 翻译为字符 → UART 回显
ISR_KBC:
	IN BYTE D1, 0x1A			; 读扫描码（清 OBF → 解除电平中断）
	;; ---- 扩展键第二字节忽略 ----
	LET R, DWORD 0x2201
	LR BYTE X, *R
	LET C, DWORD 0
	CMP DWORD X					; C = -skip
	LET E, DWORD ISR_CHK_E0
	JZ							; skip==0 → 正常
	ZERO X
	STO BYTE X					; 清除 skip 标志
	IRET
ISR_CHK_E0:
	;; ---- 0xE0 前缀: 置 skip 并忽略 ----
	LET X, DWORD 0xE0
	MOV C, D1
	CMP DWORD X					; C = D1 - 0xE0
	LET E, DWORD ISR_SET_SKIP
	JZ							; D1 == 0xE0 → 置 skip
	LET E, DWORD ISR_CHK_BREAK
	JMP
ISR_SET_SKIP:
	LET R, DWORD 0x2201
	LET A, DWORD 1
	STO BYTE A					; skip = 1
	IRET
ISR_CHK_BREAK:
	;; ---- break 码 (>= 0x80): Shift 松开则清 shift, 其余忽略 ----
	LET X, DWORD 0x80
	MOV C, D1
	CMP DWORD X					; C = D1 - 0x80
	LET E, DWORD ISR_BREAK
	JB DWORD X					; 无符号 C < X → D1 >= 0x80 → break
	LET E, DWORD ISR_CHK_SHIFT
	JMP
ISR_BREAK:
	LET X, DWORD 0xAA			; LShift break
	MOV C, D1
	CMP DWORD X
	LET E, DWORD ISR_SHIFT_OFF
	JZ
	LET X, DWORD 0xB6			; RShift break
	MOV C, D1
	CMP DWORD X
	LET E, DWORD ISR_SHIFT_OFF
	JZ
	IRET						; 其它 break 忽略
ISR_SHIFT_OFF:
	LET R, DWORD 0x2200
	ZERO X
	STO BYTE X					; shift = 0
	IRET
ISR_CHK_SHIFT:
	;; ---- Shift make (0x2A/0x36): shift = 1 ----
	LET X, DWORD 0x2A
	MOV C, D1
	CMP DWORD X
	LET E, DWORD ISR_SHIFT_ON
	JZ
	LET X, DWORD 0x36
	MOV C, D1
	CMP DWORD X
	LET E, DWORD ISR_SHIFT_ON
	JZ
	LET E, DWORD ISR_CHK_SPECIAL
	JMP
ISR_SHIFT_ON:
	LET R, DWORD 0x2200
	LET A, DWORD 1
	STO BYTE A					; shift = 1
	IRET
ISR_CHK_SPECIAL:
	;; ---- Enter: 换行 ----
	LET X, DWORD 0x1C
	MOV C, D1
	CMP DWORD X
	LET E, DWORD ISR_ENTER
	JZ
	;; ---- Backspace: 退格 (\b 空格 \b) ----
	LET X, DWORD 0x0E
	MOV C, D1
	CMP DWORD X
	LET E, DWORD ISR_BS
	JZ
	;; ---- Tab: 制表 ----
	LET X, DWORD 0x0F
	MOV C, D1
	CMP DWORD X
	LET E, DWORD ISR_TAB
	JZ
	;; ---- 其它: 查表翻译 ----
	LET E, DWORD ISR_LOOKUP
	JMP
ISR_ENTER:
	LET A, BYTE 0x0A
	OUT BYTE 0x16, A
	IRET
ISR_BS:
	LET A, BYTE 0x08
	OUT BYTE 0x16, A
	LET A, BYTE 0x20
	OUT BYTE 0x16, A
	LET A, BYTE 0x08
	OUT BYTE 0x16, A
	IRET
ISR_TAB:
	LET A, BYTE 0x09
	OUT BYTE 0x16, A
	IRET
ISR_LOOKUP:
	;; 基址 = shift ? 0x2100 : 0x2000
	LET R, DWORD 0x2200
	LR BYTE A, *R				; A = shift
	LET C, DWORD 0
	CMP DWORD A
	LET E, DWORD ISR_LOOKUP_N
	JZ
	LET R, DWORD 0x2100
	LET E, DWORD ISR_LOOKUP_DO
	JMP
ISR_LOOKUP_N:
	LET R, DWORD 0x2000
ISR_LOOKUP_DO:
	ADD DWORD R, D1				; R = base + 扫描码
	LR BYTE A, *R				; A = 字符
	LET C, DWORD 0
	CMP DWORD A
	LET E, DWORD ISR_DONE
	JZ							; 未映射 → 不输出
	OUT BYTE 0x16, A
ISR_DONE:
	IRET
