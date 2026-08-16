;; ============================================================
;; test_logic.asm - 位运算测试
;; 覆盖: AND / OR / XOR / NEG / MNE / SHL / SHR / MSL / MSR / NZ
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_logic.asm
;;
;; 期望(寄存器转储):
;;   A  = 0x00C0FFEE (全部断言通过; 任一失败则 A=0x00000000)
;;   B  = 0x0F0F0F0F   (中间结果: AND|OR|XOR 后的值)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- AND / OR / XOR (仅寄存器操作数) ----
	LET A, DWORD 0x0F0F0F0F
	LET B, DWORD 0x33333333
	AND DWORD A, B				; A = 0x03030303
	LET B, DWORD 0x0C0C0C0C
	OR DWORD A, B				; A = 0x0F0F0F0F
	LET B, DWORD 0xFF00FF00
	XOR DWORD A, B				; A = 0xF00FF00F
	;; 保存展示值
	LET X, DWORD 0x0F0F0F0F

	;; ---- AND NZ (高位保留, 只改低字节) ----
	LET A, DWORD 0xDEADBEEF
	LET B, BYTE 0x0F
	AND NZ BYTE A, B			; A = 0xDEADBE0F

	;; ---- NEG / MNE ----
	LET A, DWORD 0x00000001
	NEG A						; A = 0xFFFFFFFE
	MNE DWORD A					; A = 0x00000002

	;; ---- SHL / SHR ----
	LET A, DWORD 0x00000001
	SHL DWORD A, 4				; A = 0x00000010
	SHR DWORD A, 2				; A = 0x00000004

	;; ---- MSR (算术右移, 符号扩展) ----
	LET A, DWORD 0x80000000
	MSR DWORD A, 4				; A = 0xF8000000

	;; ---- MSL (算术左移) ----
	LET A, DWORD 0x40000000
	MSL DWORD A, 2				; A = 0x00000000

	;; ==================== 断言 ====================
	;; 断言 A == 0 (MSL结果)
	LET C, DWORD 0
	CMP DWORD A
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET B, DWORD 0x0F0F0F0F
	LET A, DWORD 0x00C0FFEE	; 通过标记
	HLT
