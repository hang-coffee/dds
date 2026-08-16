;; ============================================================
;; test_kbc.asm - AT 兼容键盘控制器 (KBC) 测试
;; 覆盖: 8042 自测/接口测试/命令字节读写/禁用启用, 键盘命令回显/复位,
;;       输出缓冲 OBF 状态, 键盘中断 (IRQ1→ISR, 电平式)
;;
;; ICT: 0x11 (ISRB=0x10, IRQ1) @ 0x1088 = ISR_KBC; KSP 不需要(SS=0)
;; 标志: 0x3100 isr_flag
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_kbc.asm
;;
;; 期望(寄存器转储):
;;   A  = 0x0B11C45E (全部断言通过; 任一失败则 A=0x00000000)
;; ============================================================

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
	;; GIE=1, ISRB=0x10 (IRQ1 → 中断号 0x11)
	LET D1, DWORD 0x80100000
	SETB RIN3_CTRL, D1

	;; ===== 1. 8042 自测: 0xAA → 0x55, OBF/SYS 状态 =====
	LET A, BYTE 0xAA
	OUT BYTE 0x1B, A			; 自测命令
	;; 断言状态 OBF (0x1B bit0) == 1
	IN BYTE D1, 0x1B
	LET X, DWORD 1
	AND DWORD D1, X
	LET C, DWORD 1
	CMP DWORD D1
	LET E, DWORD T1
	JZ
	ZERO A
	HLT
T1:
	;; 断言读 0x1A == 0x55
	IN BYTE D1, 0x1A
	LET X, DWORD 0x55
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T2
	JZ
	ZERO A
	HLT
T2:
	;; 断言读后 OBF 清 0
	IN BYTE D1, 0x1B
	LET X, DWORD 1
	AND DWORD D1, X
	LET C, DWORD 0
	CMP DWORD D1
	LET E, DWORD T3
	JZ
	ZERO A
	HLT

	;; ===== 2. 接口测试: 0xAB → 0x00 =====
T3:
	LET A, BYTE 0xAB
	OUT BYTE 0x1B, A
	IN BYTE D1, 0x1A
	LET X, DWORD 0
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T4
	JZ
	ZERO A
	HLT

	;; ===== 3. 键盘命令: 回显 0xEE / 复位 0xFF =====
T4:
	LET A, BYTE 0xEE
	OUT BYTE 0x1A, A			; 回显命令
	IN BYTE D1, 0x1A			; → 0xEE
	LET X, DWORD 0xEE
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T5
	JZ
	ZERO A
	HLT
T5:
	LET A, BYTE 0xFF
	OUT BYTE 0x1A, A			; 复位命令
	IN BYTE D1, 0x1A			; → 0xFA (ACK)
	LET X, DWORD 0xFA
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T6
	JZ
	ZERO A
	HLT
T6:
	IN BYTE D1, 0x1A			; → 0xAA (自检通过)
	LET X, DWORD 0xAA
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T7
	JZ
	ZERO A
	HLT

	;; ===== 4. 命令字节: 初始 0x00, 写入流程, 禁用/启用 =====
T7:
	LET A, BYTE 0x20			; 读命令字节
	OUT BYTE 0x1B, A
	IN BYTE D1, 0x1A			; → 0x00 (初始)
	LET X, DWORD 0
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T8
	JZ
	ZERO A
	HLT
T8:
	LET A, BYTE 0x60			; 写命令字节
	OUT BYTE 0x1B, A
	LET A, BYTE 0x00
	OUT BYTE 0x1A, A			; 命令字节 = 0x00
	LET A, BYTE 0x20
	OUT BYTE 0x1B, A
	IN BYTE D1, 0x1A			; → 0x00 (写入流程验证)
	LET X, DWORD 0
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T9
	JZ
	ZERO A
	HLT
T9:
	;; 禁用键盘 (0xAD) → 命令字节 bit3=1
	LET A, BYTE 0xAD
	OUT BYTE 0x1B, A
	LET A, BYTE 0x20
	OUT BYTE 0x1B, A
	IN BYTE D1, 0x1A			; → 0x08
	LET X, DWORD 0x08
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T10
	JZ
	ZERO A
	HLT
T10:
	;; 启用键盘 (0xAE) → 命令字节 bit3=0
	LET A, BYTE 0xAE
	OUT BYTE 0x1B, A
	LET A, BYTE 0x20
	OUT BYTE 0x1B, A
	IN BYTE D1, 0x1A			; → 0x00
	LET X, DWORD 0
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T11
	JZ
	ZERO A
	HLT

	;; ===== 5. 键盘中断: 使能 IRQ, 自测置 OBF → IRQ1 → ISR =====
T11:
	LET A, BYTE 0x60			; 写命令字节
	OUT BYTE 0x1B, A
	LET A, BYTE 0x01			; bit0: 键盘中断使能
	OUT BYTE 0x1A, A
	LET A, BYTE 0xAA			; 自测 → OBF=0x55 → 电平式中断请求
	OUT BYTE 0x1B, A
	;; 忙等 isr_flag == 1（ISR 读 0x1A=0x55 并清 OBF 解除中断）
KBC_WAIT:
	LET R, DWORD 0x3100
	LR DWORD D1, *R
	LET C, DWORD 1
	CMP DWORD D1
	LET E, DWORD PASS
	JZ
	LET E, DWORD KBC_WAIT
	JMP

PASS:
	LET A, DWORD 0x0B11C45E		; 通过标记
	HLT

;; ==================== ISR ====================
ISR_KBC:
	IN BYTE D1, 0x1A			; 读输出缓冲（清 OBF → 解除电平中断）
	LET X, DWORD 0x55
	MOV C, D1
	CMP DWORD X
	LET E, DWORD ISR_OK
	JZ
	LET R, DWORD 0x3100
	ZERO A
	STO DWORD A					; isr_flag = 0 (失败)
	IRET
ISR_OK:
	LET R, DWORD 0x3100
	LET A, DWORD 1
	STO DWORD A					; isr_flag = 1
	IRET
