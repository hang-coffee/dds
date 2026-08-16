;; ============================================================
;; test_rep.asm - REP 前缀测试
;; 覆盖: REP XCHG（新语义: 以 C 为计数器重复执行 while(C!=0){执行; C--;}）
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_rep.asm
;;
;; 期望(寄存器转储):
;;   A  = 0x0000E2E2 (全部断言通过; 任一失败则 A=0x00000000)
;;   B  = 0x22222222 (C=3 三次交换后)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- C=0 -> REP XCHG 不执行 ----
	ZERO C
	LET A, DWORD 0x11111111
	LET B, DWORD 0x22222222
	REP XCHG A, B				; C=0 -> 重复0次, 不交换

	;; 断言 A == 0x11111111
	LET C, DWORD 0x11111111
	CMP DWORD A
	LET E, DWORD NEXT
	JZ
	ZERO A
	HLT

NEXT:
	;; ---- C=1 -> REP XCHG 执行1次 (A=0x22222222, B=0x11111111) ----
	LET C, DWORD 1
	REP XCHG A, B

	;; 断言 A == 0x22222222
	LET C, DWORD 0x22222222
	CMP DWORD A
	LET E, DWORD NEXT2
	JZ
	ZERO A
	HLT

NEXT2:
	;; ---- C=2 -> REP XCHG 执行2次 (交换两次, 净效果=不变) ----
	;; 进入时 A=0x22222222, B=0x11111111
	LET C, DWORD 2
	REP XCHG A, B				; 两次交换 -> 仍为 A=0x22222222, B=0x11111111

	;; 断言 A == 0x22222222
	LET C, DWORD 0x22222222
	CMP DWORD A
	LET E, DWORD NEXT3
	JZ
	ZERO A
	HLT

NEXT3:
	;; ---- C=3 -> REP XCHG 执行3次 (净效果=交换1次) ----
	;; 进入时 A=0x22222222, B=0x11111111 -> 三次后 A=0x11111111, B=0x22222222
	LET C, DWORD 3
	REP XCHG A, B

	;; 断言 A == 0x11111111
	LET C, DWORD 0x11111111
	CMP DWORD A
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x0000E2E2		; 通过标记
	HLT
