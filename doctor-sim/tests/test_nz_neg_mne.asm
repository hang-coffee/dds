;; ============================================================
;; test_nz_neg_mne.asm - NEG/MNE 的 NZ 变体测试
;; 覆盖: MNE NZ BYTE / MNE NZ WORD / MNENZ 拼接 / NEG NZ BYTE / NEGNZ WORD
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_nz_neg_mne.asm
;;
;; 期望(寄存器转储):
;;   A  = 0x1E5E0F0F (全部断言通过; 任一失败则 A=0x00000000)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- MNE NZ BYTE: 低字节算术取反, 高位保留 ----
	LET A, DWORD 0xDEADBE01
	MNE NZ BYTE A				; 0x01 → -1 → 0xFF → A = 0xDEADBEFF
	LET X, DWORD 0xDEADBEFF
	MOV C, A
	CMP DWORD X
	LET E, DWORD T2
	JZ
	ZERO A
	HLT

T2:
	;; ---- MNE NZ WORD: 低16位算术取反 ----
	LET A, DWORD 0xDEADBE80
	MNE NZ WORD A				; 0xBE80 → 0x4180 → A = 0xDEAD4180
	LET X, DWORD 0xDEAD4180
	MOV C, A
	CMP DWORD X
	LET E, DWORD T3
	JZ
	ZERO A
	HLT

T3:
	;; ---- MNENZ DWORD 拼接写法: 全 32 位取反 ----
	LET A, DWORD 0x00000001
	MNENZ DWORD A				; -1 → 0xFFFFFFFF
	LET X, DWORD 0xFFFFFFFF
	MOV C, A
	CMP DWORD X
	LET E, DWORD T4
	JZ
	ZERO A
	HLT

T4:
	;; ---- NEG NZ BYTE: 低字节按位取反, 高位保留 ----
	LET A, DWORD 0xDEAD0000
	NEG NZ BYTE A				; 0x00 → ~0x00 = 0xFF → A = 0xDEAD00FF
	LET X, DWORD 0xDEAD00FF
	MOV C, A
	CMP DWORD X
	LET E, DWORD T5
	JZ
	ZERO A
	HLT

T5:
	;; ---- NEGNZ WORD: 低16位按位取反 ----
	LET A, DWORD 0xDEAD0F0F
	NEGNZ WORD A				; 0x0F0F → 0xF0F0 → A = 0xDEADF0F0
	LET X, DWORD 0xDEADF0F0
	MOV C, A
	CMP DWORD X
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x1E5E0F0F		; 通过标记
	HLT
