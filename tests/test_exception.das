;; ============================================================
;; test_exception.das - 异常体系端到端测试
;; 覆盖: #DIV(0x00) / #II(0x01) / #STACK(0x02, 上溢+下溢) / #GP(0x03, 特权+跳转)
;;       异常自动压栈 RIN3/P/中断号、强制切KSP、XAR 写入、IRET 恢复
;;
;; 数据布局:
;;   ICT  @ 0x1000 (表项 0x00-0x03)
;;   KSP  = 0x4000, 用户栈 S = 0x3000
;;   标志区: 0x3100 flag_div, 0x3104 stack_xar, 0x3108 flag_ii, 0x310C gp_xar
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_exception.das
;;
;; 期望(寄存器转储):
;;   A  = 0x00E3C3E3 (全部断言通过; 任一失败则 A=0x00000000)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- 初始化: ICT / KSP / S / GIE ----
	;; ICT[0] (#DIV) @ 0x1000
	LET R, DWORD 0x1000
	LET A, DWORD ISR_DIV
	STO DWORD A
	LET A, DWORD 0
	STO BYTE A
	;; ICT[1] (#II) @ 0x1008
	LET R, DWORD 0x1008
	LET A, DWORD ISR_II
	STO DWORD A
	LET A, DWORD 0
	STO BYTE A
	;; ICT[2] (#STACK) @ 0x1010
	LET R, DWORD 0x1010
	LET A, DWORD ISR_STACK
	STO DWORD A
	LET A, DWORD 0
	STO BYTE A
	;; ICT[3] (#GP) @ 0x1018
	LET R, DWORD 0x1018
	LET A, DWORD ISR_GP
	STO DWORD A
	LET A, DWORD 0
	STO BYTE A
	LET R, DWORD 0x1000
	SETB ICTB, R
	LET R, DWORD 0x4000
	SETB KSP, R
	LET S, DWORD 0x3000
	LET D1, DWORD 0x80000000	; GIE=1
	SETB RIN3_CTRL, D1

	;; ============ 1. #DIV (除数为0) ============
	LET D1, DWORD 5
	LET D2, DWORD 0
DIV_INSTR:
	DIV DWORD D1, D2			; → #DIV, XAR = DIV_INSTR
DIV_AFTER:
	;; 断言 flag_div (0x3100) == 1
	LET R, DWORD 0x3100
	LR DWORD D1, *R
	LET C, DWORD 1
	CMP DWORD D1
	LET E, DWORD II_SETUP
	JZ
	ZERO A
	HLT

	;; ============ 2. #II (非法指令) ============
II_SETUP:
	;; 在代码 0x2000 处写两条 0x00（解码为非法指令）
	LET E, DWORD 0x2000
	ZERO A
	ST BYTE *E, A
	INC E
	ST BYTE *E, A
	LET E, DWORD 0x2000
	JMP							; → 执行 0x00 0x00 → #II, XAR=0x2000
II_AFTER:
	;; 断言 flag_ii (0x3108) == 1
	LET R, DWORD 0x3108
	LR DWORD D1, *R
	LET C, DWORD 1
	CMP DWORD D1
	LET E, DWORD STK_SETUP
	JZ
	ZERO A
	HLT

	;; ============ 3. #STACK 上溢 (SFA 越界) ============
STK_SETUP:
	LET S, DWORD 0x3000
	SFA DWORD 0x1000000			; S+0x1000000 > DATA_SIZE → #STACK 上溢, XAR=0x1003000
STK1_AFTER:
	;; 断言 stack_xar (0x3104) == 0x1003000
	LET R, DWORD 0x3104
	LR DWORD D1, *R
	LET X, DWORD 0x1003000
	MOV C, D1
	CMP DWORD X
	LET E, DWORD STK2_SETUP
	JZ
	ZERO A
	HLT

	;; ============ 4. #STACK 下溢 (POP 空栈) ============
STK2_SETUP:
	LET S, DWORD 0
	POP DWORD A					; S=0 < 4 → #STACK 下溢, XAR=0xFFFFFFFC
STK2_AFTER:
	;; 断言 stack_xar (0x3104) == 0xFFFFFFFC
	LET R, DWORD 0x3104
	LR DWORD D1, *R
	LET X, DWORD 0xFFFFFFFC
	MOV C, D1
	CMP DWORD X
	LET E, DWORD GP_SETUP
	JZ
	ZERO A
	HLT

	;; ============ 5. #GP 特权 (用户态执行 SETB) ============
GP_SETUP:
	LET D1, DWORD 0x90000000	; GIE=1, CPL=1 → 用户态
	SETB RIN3_CTRL, D1
	LET D1, DWORD 0x55
GP_INSTR:
	SETB CBASE, D1				; 用户态特权指令 → #GP, XAR = GP_INSTR
GP_AFTER:
	;; 断言 gp_xar (0x310C) == GP_INSTR
	LET R, DWORD 0x310C
	LR DWORD D1, *R
	LET X, DWORD GP_INSTR
	MOV C, D1
	CMP DWORD X
	LET E, DWORD GP2_SETUP
	JZ
	ZERO A
	HLT

	;; ============ 6. 写 E 到物理范围外 → #GP (无效代码指针) ============
	;; 注: 内存物理边界检查（mem_check）总是生效，MPU=0 时 LET/MOV 写
	;;     超出 CODE_SIZE 的 E 也会被拒（#GP）；JMP 的目标检查为防御性。
GP2_SETUP:
	LET E, DWORD 0x7FFFFFFF		; 超出 CODE_SIZE → #GP, XAR = GP2_SETUP
GP2_AFTER:
	;; 断言 gp_xar (0x310C) == GP2_SETUP
	LET R, DWORD 0x310C
	LR DWORD D1, *R
	LET X, DWORD GP2_SETUP
	MOV C, D1
	CMP DWORD X
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x00E3C3E3		; 通过标记
	HLT

;; ==================== 异常 ISR ====================
;; 异常强制 SS=1（切到KSP 0x4000 执行）

;; #DIV: 验证 XAR == DIV_INSTR
ISR_DIV:
	GETB D1, XAR
	LET X, DWORD DIV_INSTR
	MOV C, D1
	CMP DWORD X
	LET E, DWORD ISR_DIV_OK
	JZ
	LET R, DWORD 0x3100
	ZERO A
	STO DWORD A					; flag_div = 0 (失败)
	IRET
ISR_DIV_OK:
	LET R, DWORD 0x3100
	LET A, DWORD 1
	STO DWORD A					; flag_div = 1
	IRET

;; #II: 验证 XAR == 0x2000；并修补栈上的返回地址跳到 II_AFTER
;; （IRET 默认返回坏指令的下一字节 0x2002，那里是未初始化代码区；
;;   故覆盖异常帧中的 P 字段——异常帧布局(S=0x3000 时):
;;   [0x3001..0x3004]ictb, [0x3005..0x3008]ctrl, [0x3009..0x300C]P, [0x300D]中断号）
ISR_II:
	GETB D1, XAR
	LET X, DWORD 0x2000
	MOV C, D1
	CMP DWORD X
	LET E, DWORD ISR_II_OK
	JZ
	LET R, DWORD 0x3108
	ZERO A
	STO DWORD A					; flag_ii = 0 (失败)
	IRET
ISR_II_OK:
	LET R, DWORD 0x3009			; 异常帧中的 P 字段
	LET A, DWORD II_AFTER
	ST DWORD *R, A				; 覆盖返回地址 → II_AFTER
	LET R, DWORD 0x3108
	LET A, DWORD 1
	STO DWORD A					; flag_ii = 1
	IRET

;; #STACK: 记录 XAR
ISR_STACK:
	GETB D1, XAR
	LET R, DWORD 0x3104
	STO DWORD D1					; data[0x3104] = XAR
	IRET

;; #GP: 记录 XAR
ISR_GP:
	GETB D1, XAR
	LET R, DWORD 0x310C
	STO DWORD D1					; data[0x310C] = XAR
	IRET
