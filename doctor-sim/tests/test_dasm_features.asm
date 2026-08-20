;; ============================================================
;; test_dasm_features.asm - dasm 新语法端到端测试
;; 覆盖: *reg+N 指针偏移 / PUSH P / pushp / NZ 拼接(ADDNZ) / 表达式
;;       (同时验证模拟器解码器对 LR/ST 偏移立即数的支持)
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_dasm_features.asm
;;
;; 期望(寄存器转储):
;;   A  = 0x00D5FE5E (全部断言通过; 任一失败则 A=0x00000000)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ============ 1. LR/ST 指针偏移 (*reg+N) ============
	LET R, DWORD 0x2000
	LET A, DWORD 0x11223344
	STO DWORD A					; [0x2000] = 0x11223344, R = 0x2004
	LET A, DWORD 0x55667788
	STO DWORD A					; [0x2004] = 0x55667788, R = 0x2008

	;; LR DWORD D1, *R-8   → D1 = [0x2000] = 0x11223344
	LR DWORD D1, *R-8
	;; LR BYTE X, *R-6     → X = [0x2002] = 0x22 (LE: [0x2000]=0x44,[0x2001]=0x33,[0x2002]=0x22)
	LR BYTE X, *R-6
	MOV D2, X					; 保存 X
	;; ST DWORD *R-4, D1   → [0x2004] = D1 = 0x11223344
	ST DWORD *R-4, D1

	;; 断言 D1 == 0x11223344
	LET X, DWORD 0x11223344
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T1
	JZ
	ZERO A
	HLT
T1:
	;; 断言 D2 == 0x22 (BYTE 偏移读取)
	LET C, BYTE 0x22
	CMP BYTE D2
	LET E, DWORD T2
	JZ
	ZERO A
	HLT

T2:
	;; ============ 2. NZ 拼接形式 (ADDNZ) ============
	LET A, DWORD 0xDEADBE00
	ADDNZ BYTE A, 0x21			; 高位保留 + 低字节加: A = 0xDEADBE21
	LET X, DWORD 0xDEADBE21
	MOV C, A
	CMP DWORD X
	LET E, DWORD T3
	JZ
	ZERO A
	HLT

T3:
	;; ============ 3. PUSH P / pushp ============
	PUSH DWORD P				; 压入 P（= T_PUSHP 地址）
T_PUSHP:
	POP DWORD D1					; D1 = T_PUSHP
	LET X, DWORD T_PUSHP
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T3B
	JZ
	ZERO A
	HLT
T3B:
	pushp						; 关键字形式（隐含 DWORD, 压入 P）
T_PUSHP2:
	POP DWORD D2					; D2 = T_PUSHP2
	LET X, DWORD T_PUSHP2
	MOV C, D2
	CMP DWORD X
	LET E, DWORD T4
	JZ
	ZERO A
	HLT

T4:
	;; ============ 4. 表达式 (标号偏移 / $) ============
	;; D1 = T5 - START + 4 = T5 + 4 (START = 0)
	LET D1, DWORD T5 - START + 4
	LET X, DWORD T5 + 4			; 表达式做立即数
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T5
	JZ
	ZERO A
	HLT
T5:
	;; $ 展开 = 当前指令起始地址
	LET D1, DWORD $				; D1 = T5 (本条 LET 的地址)
	MOV C, D1
	LET X, DWORD T5
	CMP DWORD X
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x00D5FE5E		; 通过标记
	HLT
