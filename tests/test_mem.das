;; ============================================================
;; test_mem.das - 内存访问测试
;; 覆盖: SR / ST / LR / LOD / STO / BLKS / PUSH / POP / POR
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_mem.das
;;
;; 期望(寄存器转储):
;;   A  = 0x0000E4E4 (全部断言通过; 任一失败则 A=0x00000000)
;;   D1 = 0xCAFEBABE (POR 弹栈写入 *R 后读回)
;;   X  = 0xAAAAAAAA (BLKS 批量填充后读回)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- SR 计算地址: R = T + X*4 + 2 = 0x1000 + 16 + 2 = 0x100E ----
	LET T, DWORD 0x1000
	LET X, DWORD 4
	SR DWORD T + X*4 + 2		; R = 0x100E

	;; ---- ST / LR 写读 ----
	LET A, DWORD 0x11223344
	ST DWORD *R, A				; data[0x100E] = 0x11223344
	ZERO B
	LR DWORD B, *R				; B = 0x11223344

	;; ---- LOD / STO 自动递增 ----
	LET R, DWORD 0x2000
	LET A, DWORD 0x00000001
	STO DWORD A					; data[0x2000] = 1, R += 4
	STO DWORD A					; data[0x2004] = 1, R += 4
	ZERO A
	LOD DWORD A					; A = data[0x2008] = 0, R += 4

	;; ---- BLKS 批量填充: 从 R 起 C 个 DWORD = A ----
	LET R, DWORD 0x3000
	LET C, DWORD 8
	LET A, DWORD 0xAAAAAAAA
	BLKS DWORD A				; data[0x3000..0x301F] = 0xAAAAAAAA
	;; 读回第4个 (0x3000 + 3*4 = 0x300C)
	LET R, DWORD 0x300C
	LR DWORD X, *R				; X = 0xAAAAAAAA

	;; ---- PUSH / POP ----
	LET A, DWORD 0xDEADBEEF
	PUSH DWORD A
	ZERO A
	POP DWORD A					; A = 0xDEADBEEF

	;; ---- POR: 弹栈写入 *R ----
	PUSH DWORD 0xCAFEBABE
	LET R, DWORD 0x4000
	POR DWORD *R				; data[0x4000] = 0xCAFEBABE
	LR DWORD D1, *R				; D1 = 0xCAFEBABE

	;; ==================== 断言 ====================
	;; 断言 D1 == 0xCAFEBABE
	LET C, DWORD 0xCAFEBABE
	CMP DWORD D1
	LET E, DWORD CHK_X
	JZ
	ZERO A
	HLT

CHK_X:
	;; 断言 X == 0xAAAAAAAA
	LET C, DWORD 0xAAAAAAAA
	CMP DWORD X
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x0000E4E4		; 通过标记
	HLT
