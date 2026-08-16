;; ============================================================
;; test_int_gie.asm - 软件中断在 GIE=0 时仍可触发
;; 覆盖: INT(0x32) 不受全局中断开关影响（manual: 软中断总是尝试派发）
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_int_gie.asm
;;
;; 期望(寄存器转储):
;;   A  = 0x0A11E6E0 (ISR 执行了一次; 失败则 A=0x00000000)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- ICT[0x10] @ 0x1080 = ISR_INT ----
	LET R, DWORD 0x1080
	LET A, DWORD ISR_INT
	STO DWORD A
	LET A, DWORD 0
	STO BYTE A
	LET R, DWORD 0x1000
	SETB ICTB, R

	;; ---- GIE=0（RIN3_CTRL=0）, 处于用户态亦可 ----
	LET D1, DWORD 0
	SETB RIN3_CTRL, D1

	;; ---- INT 0x10 在 GIE=0 时也应派发（不 #GP）----
INT_INSTR:
	INT 0x10
INT_AFTER:
	;; 断言 flag (0x2000) == 1
	LET R, DWORD 0x2000
	LR DWORD D1, *R
	LET C, DWORD 1
	CMP DWORD D1
	LET E, DWORD CHK2
	JZ
	ZERO A
	HLT
CHK2:
	;; 断言 XAR 未变（INT 不是异常，XAR 保持 0）
	GETB D1, XAR
	LET C, DWORD 0
	CMP DWORD D1
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x0A11E6E0		; 通过标记
	HLT

;; ==================== ISR ====================
ISR_INT:
	LET R, DWORD 0x2000
	LET A, DWORD 1
	STO DWORD A					; flag = 1
	IRET
