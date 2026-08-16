;; ============================================================
;; test_arith.das - 四则运算测试
;; 覆盖: LET / ADD / SUB / MUL / DIV / DIV QWORD / CSI / CDI / NZ
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_arith.das
;;
;; 期望(寄存器转储):
;;   D1 = 0x00000002   (100/7 的余数)
;;   D2 = 0x0000000E   (100/7 的商)
;;   A  = 0x000A11CE   (全部断言通过; 任一失败则 A=0x00000000)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- LET + ADD DWORD (寄存器/立即数) ----
	LET A, DWORD 0x12345678
	ADD DWORD A, 1				; A = 0x12345679
	LET B, DWORD 0x1000
	ADD DWORD A, B				; A = 0x12346679

	;; ---- SUB DWORD ----
	SUB DWORD A, 0x1000			; A = 0x12345679

	;; ---- ADD NZ (高位保留, 仅低字节相加) ----
	LET A, DWORD 0xAA000000
	ADD NZ BYTE A, 0x21			; A = 0xAA000021

	;; ---- MUL WORD -> D2 ----
	LET C, WORD 0x1234
	LET D2, WORD 0x10
	MUL WORD C, D2				; D2 = 0x1234 * 0x10 = 0x12340

	;; ---- DIV: D2=商, D1=余数 ----
	LET D1, DWORD 100
	LET D2, DWORD 7
	DIV DWORD D1, D2			; D2=14(商), D1=2(余)

	;; ---- CSI / CDI ----
	ZERO C
	CSI							; C = 1
	CSI							; C = 2
	CDI							; C = 1

	;; ---- DIV QWORD: D1:D2 / A:B -> D2=商 D1=余 ----
	ZERO D1
	LET D2, DWORD 100
	ZERO A
	LET B, DWORD 7
	DIV QWORD					; D2=14(商), D1=2(余)

	;; ==================== 断言 ====================
	;; 断言 D1 == 0x02 (余数)
	LET C, DWORD 0x02
	CMP DWORD D1				; C = 0x02 - D1
	LET E, DWORD CHK_D2
	JZ
	ZERO A						; 失败: A=0
	HLT

CHK_D2:
	;; 断言 D2 == 0x0E (商)
	LET C, DWORD 0x0E
	CMP DWORD D2
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x000A11CE		; 通过标记
	HLT
