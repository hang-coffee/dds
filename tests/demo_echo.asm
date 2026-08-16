;; ============================================================
;; demo_echo.asm - 键盘回显演示（演示型，非自动化测试）
;;
;; 功能: 宿主终端按键 → KBC 设备（Set 1 扫描码, 经 input.c 注入）
;;       → KBC 中断 (IRQ1→0x11) → ISR 读扫描码 → 以十六进制经 UART 回显
;;       （UART 输出走 Display 层, 直接显示在终端）
;;
;; 运行:
;;   ./tools/dasm/dasm tests/demo_echo.asm demo_code.bin demo_data.bin
;;   ./build/bin/doctor_sim -f code demo_code.bin data demo_data.bin
;;
;;   然后在模拟器终端打字, 每个按键显示其 Set 1 make 码 (两位十六进制)。
;;   Ctrl+C 暂停/恢复模拟; 暂停时按 q 退出。
;;
;; 数据布局:
;;   0x2200: "0123456789ABCDEF" 十六进制字符表
;;   0x2300: 提示字符串 "KBD echo: "
;; ============================================================

	SECTION DATA
	ORG 0x2200
HEXMAP:
	DB 0x2200, "0123456789ABCDEF"
	ORG 0x2300
MSG:
	DB 0x2300, "KBD echo: "

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
	LET C, DWORD 10
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
;; 读 KBC 扫描码 → 十六进制两位 + 空格 → UART 回显
ISR_KBC:
	IN BYTE D1, 0x1A			; 读扫描码（清 OBF → 解除电平中断）
	;; 高半字节
	MOV A, D1
	SHR BYTE A, 4
	LET R, DWORD 0x2200
	ADD DWORD R, A
	LR BYTE A, *R				; hex 字符
	OUT BYTE 0x16, A
	;; 低半字节（AND 只接受寄存器操作数）
	LET X, BYTE 0x0F
	MOV B, D1
	AND BYTE B, X				; B = D1 & 0x0F
	MOV A, B
	LET R, DWORD 0x2200
	ADD DWORD R, A
	LR BYTE A, *R
	OUT BYTE 0x16, A
	;; 空格分隔
	LET A, BYTE 0x20
	OUT BYTE 0x16, A
	IRET
