;; ============================================================
;; test_mem_fault.asm - 数据内存物理边界检查测试 (MPU=0)
;; 覆盖: LR/ST/LOD/STO 越界→#GP, SFA 物理上溢→#STACK, LET E 越界→#GP,
;;       边界内访问正常
;;
;; 物理边界: DATA_SIZE=0x1000000, CODE_SIZE=0x1000000（MPU 关闭，仅物理检查）
;; ICT: #GP@0x1018, #STACK@0x1010; KSP=0x5000; S=0x3000
;; 标志: 0x3100 gp_count, 0x3104 gp_xar, 0x3108 stack_xar
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_mem_fault.asm
;;
;; 期望(寄存器转储):
;;   A  = 0x00FA17A1 (全部断言通过; 任一失败则 A=0x00000000)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- 初始化 ICT / KSP ----
	LET R, DWORD 0x1018
	LET A, DWORD ISR_GP
	STO DWORD A
	LET A, DWORD 0
	STO BYTE A
	LET R, DWORD 0x1010
	LET A, DWORD ISR_STACK
	STO DWORD A
	LET A, DWORD 0
	STO BYTE A
	LET R, DWORD 0x1000
	SETB ICTB, R
	LET R, DWORD 0x5000
	SETB KSP, R
	LET S, DWORD 0x3000

	;; ===== 1. LR 越界 (#GP): *R 超出 DATA_SIZE =====
	LET R, DWORD 0xFFFFFF		; DATA_SIZE-1, DWORD 读取越界
LR_INSTR:
	LR DWORD D1, *R				; → #GP
	;; 断言 gp_count == 1
	LET R, DWORD 0x3100
	LR DWORD D1, *R
	LET X, DWORD 1
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T2
	JZ
	ZERO A
	HLT

	;; ===== 2. ST 越界 (#GP) =====
T2:
	LET R, DWORD 0xFFFFFF
	ST DWORD *R, D1				; → #GP
	;; 断言 gp_count == 2
	LET R, DWORD 0x3100
	LR DWORD D1, *R
	LET X, DWORD 2
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T3
	JZ
	ZERO A
	HLT

	;; ===== 3. LOD / STO 越界 (#GP) =====
T3:
	LET R, DWORD 0xFFFFFF
	LOD DWORD D1					; → #GP
	;; 断言 gp_count == 3
	LET R, DWORD 0x3100
	LR DWORD D1, *R
	LET X, DWORD 3
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T4
	JZ
	ZERO A
	HLT
T4:
	LET R, DWORD 0xFFFFFF
	STO DWORD D1					; → #GP
	;; 断言 gp_count == 4
	LET R, DWORD 0x3100
	LR DWORD D1, *R
	LET X, DWORD 4
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T5
	JZ
	ZERO A
	HLT

	;; ===== 4. SFA 物理上溢 (#STACK) =====
T5:
	LET S, DWORD 0x3000
	SFA DWORD 0x1000000			; S 新值 = 0x1003000 ≥ DATA_SIZE → #STACK
	;; 断言 stack_xar (0x3108) == 0x1003000 (上溢, bit31=0)
	LET R, DWORD 0x3108
	LR DWORD D1, *R
	LET X, DWORD 0x1003000
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T6
	JZ
	ZERO A
	HLT

	;; ===== 5. LET E 物理越界 (#GP) =====
T6:
	LET S, DWORD 0x3000
LETE_INSTR:
	LET E, DWORD 0x7FFFFFFF		; 超出 CODE_SIZE → #GP
	;; 断言 gp_count == 5
	LET R, DWORD 0x3100
	LR DWORD D1, *R
	LET X, DWORD 5
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T7
	JZ
	ZERO A
	HLT

	;; ===== 6. 边界内访问正常 =====
T7:
	LET R, DWORD 0x2000
	LET A, DWORD 0x12345678
	STO DWORD A					; 写 [0x2000, 0x2004) ✓
	LR DWORD D1, *R-4			; 读回 ✓
	LET X, DWORD 0x12345678
	MOV C, D1
	CMP DWORD X
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x00FA17A1		; 通过标记
	HLT

;; ==================== ISR ====================
ISR_GP:
	GETB D1, XAR
	LET R, DWORD 0x3104
	STO DWORD D1					; gp_xar = XAR
	LET R, DWORD 0x3100
	LR DWORD D1, *R
	INC D1
	STO DWORD D1					; gp_count++
	IRET

ISR_STACK:
	GETB D1, XAR
	LET R, DWORD 0x3108
	STO DWORD D1					; stack_xar = XAR
	IRET
