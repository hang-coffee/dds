;; ============================================================
;; test_jump.das - 条件跳转测试
;; 覆盖: TEST / CMP / JZ / JNZ / JRZ / JRNZ / JA / JNA / JB / JNB
;;       / JG / JNG / JL / JNL (无符号与有符号)
;;
;; 注意: 所有条件跳转的目标都取自 E 寄存器, 跳转前必须 LET E。
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_jump.das
;;
;; 期望(寄存器转储):
;;   A  = 0x00005E5E (全部跳转点通过; 任一失败则 A=0x00000000)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ============ JZ: C == 0 ============
	ZERO C
	LET E, DWORD T_JZ
	JZ
	ZERO A						; 失败
	HLT
T_JZ:

	;; ============ JNZ: C != 0 ============
	LET C, DWORD 1
	LET E, DWORD T_JNZ
	JNZ
	ZERO A
	HLT
T_JNZ:

	;; ============ JRZ WORD C: 低16位 == 0 ============
	LET C, DWORD 0x00010000		; 低16位为0
	LET E, DWORD T_JRZ
	JRZ WORD C
	ZERO A
	HLT
T_JRZ:

	;; ============ JRNZ WORD C: 低16位 != 0 ============
	LET C, DWORD 0x00000001
	LET E, DWORD T_JRNZ
	JRNZ WORD C
	ZERO A
	HLT
T_JRNZ:

	;; ============ JA: 无符号 C > reg ============
	LET C, DWORD 10
	LET X, DWORD 5
	LET E, DWORD T_JA
	JA DWORD X					; 10 > 5 -> 跳
	ZERO A
	HLT
T_JA:

	;; ============ JNA: 无符号 C <= reg ============
	LET C, DWORD 5
	LET X, DWORD 10
	LET E, DWORD T_JNA
	JNA DWORD X					; 5 <= 10 -> 跳
	ZERO A
	HLT
T_JNA:

	;; ============ JB: 无符号 C < reg ============
	LET C, DWORD 3
	LET X, DWORD 9
	LET E, DWORD T_JB
	JB DWORD X					; 3 < 9 -> 跳
	ZERO A
	HLT
T_JB:

	;; ============ JNB: 无符号 C >= reg ============
	LET C, DWORD 9
	LET X, DWORD 3
	LET E, DWORD T_JNB
	JNB DWORD X					; 9 >= 3 -> 跳
	ZERO A
	HLT
T_JNB:

	;; ============ JG: 有符号 C > reg (负数比较) ============
	LET C, DWORD 0xFFFFFFFB		; -5
	LET X, DWORD 0xFFFFFFF6		; -10
	LET E, DWORD T_JG
	JG DWORD X					; -5 > -10 -> 跳
	ZERO A
	HLT
T_JG:

	;; ============ JNG: 有符号 C <= reg ============
	LET C, DWORD 0xFFFFFFF6		; -10
	LET X, DWORD 0xFFFFFFFB		; -5
	LET E, DWORD T_JNG
	JNG DWORD X					; -10 <= -5 -> 跳
	ZERO A
	HLT
T_JNG:

	;; ============ JL: 有符号 C < reg ============
	LET C, DWORD 0xFFFFFFF6		; -10
	LET X, DWORD 0xFFFFFFFB		; -5
	LET E, DWORD T_JL
	JL DWORD X					; -10 < -5 -> 跳
	ZERO A
	HLT
T_JL:

	;; ============ JNL: 有符号 C >= reg ============
	LET C, DWORD 0xFFFFFFFB		; -5
	LET X, DWORD 0xFFFFFFF6		; -10
	LET E, DWORD T_JNL
	JNL DWORD X					; -5 >= -10 -> 跳
	ZERO A
	HLT
T_JNL:

	;; ============ TEST: C = C & reg (低N位) ============
	LET C, DWORD 0x0F
	LET X, DWORD 0x0A
	TEST BYTE X					; C = 0x0F & 0x0A = 0x0A
	LET E, DWORD T_TEST
	JNZ							; C != 0 -> 跳
	ZERO A
	HLT
T_TEST:

	;; ============ CMP + JZ: C = C - reg ============
	LET C, DWORD 5
	LET X, DWORD 5
	CMP DWORD X					; C = 5 - 5 = 0
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x00005E5E		; 通过标记
	HLT
