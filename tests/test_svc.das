;; ============================================================
;; test_svc.das - SVC 陷入内核 + 内核态检查测试
;; 覆盖: SVC(0x40) 特权级切换 / 用户态 SETB-GETB 触发 #GP / 异常在内核态执行
;;
;; 流程:
;;   1. 用户态(CPL=1)执行 SVC → CPL=0, GIE=0, 调用中断 0xFE
;;   2. ISR_SVC(内核态)验证 CPL==0 且 GIE==0, 置 svc_flag
;;   3. IRET 恢复 RIN3 → 回到用户态(CPL=1, GIE=1)
;;   4. 用户态执行 SETB(特权指令) → #GP
;;   5. ISR_GP 验证 XAR == GP_INSTR, 且异常处理器处于内核态(CPL==0), 置 gp 标志
;;
;; 数据布局: ICT @ 0x1000 (表项 3 与 0xFE), KSP=0x4000, S=0x3000
;;   0x3120 svc_flag, 0x3124 gp_xar, 0x3128 gp_kflag
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_svc.das
;;
;; 期望(寄存器转储):
;;   A  = 0x005C5C5E (全部断言通过; 任一失败则 A=0x00000000)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- ICT[3] (#GP) @ 0x1018 ----
	LET R, DWORD 0x1018
	LET A, DWORD ISR_GP
	STO DWORD A
	LET A, DWORD 0
	STO BYTE A
	;; ---- ICT[0xFE] (SVC) @ 0x17F0 ----
	LET R, DWORD 0x17F0
	LET A, DWORD ISR_SVC
	STO DWORD A
	LET A, DWORD 0
	STO BYTE A
	LET R, DWORD 0x1000
	SETB ICTB, R
	LET R, DWORD 0x4000
	SETB KSP, R
	LET S, DWORD 0x3000

	;; ---- 进入用户态: CPL=1, GIE=1 ----
	LET D1, DWORD 0x90000000
	SETB RIN3_CTRL, D1

	;; ============ 1. SVC ============
SVC_INSTR:
	SVC							; CPL→0, GIE→0, 中断 0xFE
SVC_AFTER:
	;; IRET 后回到用户态 (CPL=1, GIE=1)
	;; 断言 svc_flag (0x3120) == 1
	LET R, DWORD 0x3120
	LR DWORD D1, *R
	LET C, DWORD 1
	CMP DWORD D1
	LET E, DWORD GP_TEST
	JZ
	ZERO A
	HLT

	;; ============ 2. 用户态 SETB → #GP ============
GP_TEST:
	LET D1, DWORD 0x55
GP_INSTR:
	SETB CBASE, D1				; 用户态特权指令 → #GP, XAR = GP_INSTR
GP_AFTER:
	;; 断言 gp_xar (0x3124) == GP_INSTR
	LET R, DWORD 0x3124
	LR DWORD D1, *R
	LET X, DWORD GP_INSTR
	MOV C, D1
	CMP DWORD X
	LET E, DWORD GP_KCHK
	JZ
	ZERO A
	HLT
GP_KCHK:
	;; 断言 gp_kflag (0x3128) == 1 (异常处理器在内核态)
	LET R, DWORD 0x3128
	LR DWORD D1, *R
	LET C, DWORD 1
	CMP DWORD D1
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x005C5C5E		; 通过标记
	HLT

;; ==================== ISR ====================

;; SVC 处理器: 验证 CPL==0 且 GIE==0 (SVC 硬件置位)
ISR_SVC:
	GETB D1, RIN3_CTRL
	LET X, DWORD 0x90000000		; GIE|CPL 掩码
	MOV C, D1
	AND DWORD C, X				; C = CTRL & 0x90000000
	;; 期望 0: CPL=0 且 GIE=0
	LET E, DWORD ISR_SVC_OK
	JZ
	LET R, DWORD 0x3120
	ZERO A
	STO DWORD A					; svc_flag = 0 (失败)
	IRET
ISR_SVC_OK:
	LET R, DWORD 0x3120
	LET A, DWORD 1
	STO DWORD A					; svc_flag = 1
	IRET

;; #GP 处理器: 记录 XAR, 并验证在内核态执行 (CPL==0)
ISR_GP:
	GETB D1, XAR
	LET R, DWORD 0x3124
	STO DWORD D1					; gp_xar
	GETB D2, RIN3_CTRL
	LET X, DWORD 0x10000000		; CPL 掩码
	MOV C, D2
	AND DWORD C, X				; C = CTRL & 0x10000000
	LET E, DWORD ISR_GP_K
	JZ							; CPL==0 → 内核态
	LET R, DWORD 0x3128
	ZERO A
	STO DWORD A					; gp_kflag = 0 (失败)
	IRET
ISR_GP_K:
	LET R, DWORD 0x3128
	LET A, DWORD 1
	STO DWORD A					; gp_kflag = 1
	IRET
