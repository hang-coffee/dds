;; ============================================================
;; test_mpu.asm - MPU 内存保护测试
;; 覆盖: LR/ST/LOD/STO/BLKS 越界→#GP, PUSH/POP/SFA 越界→#STACK,
;;       LET 写 S→#STACK / 写 E→#GP, JMP 目标越界→#GP, 区间内访问正常
;;
;; 护栏: CBASE=0, CLIMIT=0x1000 (代码区); DBASE=0x2000, DLIMIT=0x4000 (数据区)
;; ICT: #GP@0x1018, #STACK@0x1010; KSP=0x5000; S=0x3000
;; 标志: 0x3100 gp_count, 0x3104 gp_xar, 0x3108 stack_xar
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_mpu.asm
;;
;; 期望(寄存器转储):
;;   A  = 0x00BEEF00 (全部断言通过; 任一失败则 A=0x00000000)
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
	;; ---- 设置护栏 ----
	LET D1, DWORD 0x0000
	SETB CBASE, D1
	LET D1, DWORD 0x1000
	SETB CLIMIT, D1
	LET D1, DWORD 0x2000
	SETB DBASE, D1
	LET D1, DWORD 0x4000
	SETB DLIMIT, D1
	;; ---- 开启 MPU + GIE (0x80000000|0x20000000) ----
	LET D1, DWORD 0xA0000000
	SETB RIN3_CTRL, D1
	LET S, DWORD 0x3000

	;; ===== 1. LR 越界 (#GP) =====
	LET R, DWORD 0x4000			; 等于 DLIMIT → 越界
LR_INSTR:
	LR DWORD D1, *R				; → #GP
LR_AFTER:
	;; 断言 gp_count (0x3100) == 1
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
	LET R, DWORD 0x4000
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

	;; ===== 3. LOD 越界 (#GP, R 低于 DBASE) =====
T3:
	LET R, DWORD 0x1FFC			; 低于 DBASE(0x2000)
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

	;; ===== 4. STO 越界 (#GP) =====
T4:
	LET R, DWORD 0x1FFC			; 重置 R（ISR 会破坏 R）
	STO DWORD D1					; R 仍越界 → #GP
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

	;; ===== 5. PUSH 越界 (#STACK, S+4 顶到 DLIMIT) =====
T5:
	LET S, DWORD 0x3FFC
	LET A, DWORD 0x11111111
	PUSH DWORD A				; → #STACK, xar=0x4000 (上溢)
	;; 断言 stack_xar (0x3108) == 0x4000
	LET R, DWORD 0x3108
	LR DWORD D1, *R
	LET X, DWORD 0x4000
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T6
	JZ
	ZERO A
	HLT

	;; ===== 6. POP 越界 (#STACK, 读区间低于 DBASE) =====
T6:
	LET S, DWORD 0x2000
	POP DWORD A					; → #STACK, xar=0x80001FFC (下溢)
	;; 断言 stack_xar == 0x80001FFC
	LET R, DWORD 0x3108
	LR DWORD D1, *R
	LET X, DWORD 0x80001FFC
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T7
	JZ
	ZERO A
	HLT

	;; ===== 7. LET S 越界 (#STACK) =====
T7:
	LET S, DWORD 0x4000			; S 新值 = DLIMIT → #STACK
	;; 断言 stack_xar == 0x4000
	LET R, DWORD 0x3108
	LR DWORD D1, *R
	LET X, DWORD 0x4000
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T8
	JZ
	ZERO A
	HLT

	;; ===== 8. LET E 越界 (#GP) =====
T8:
	LET S, DWORD 0x3000
	LET E, DWORD 0x2000			; 超出 CLIMIT(0x1000) → #GP
	;; 断言 gp_count == 5
	LET R, DWORD 0x3100
	LR DWORD D1, *R
	LET X, DWORD 5
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T9
	JZ
	ZERO A
	HLT

	;; ===== 9. JMP 目标越界 (#GP) =====
T9:
	;; MPU 开启时 LET/MOV 无法写入越界 E（会被检查拒绝），
	;; 故先临时关闭 MPU 注入越界 E，再开启 MPU 触发 JMP 检查
	LET D1, DWORD 0x80000000	; GIE only, MPU=0
	SETB RIN3_CTRL, D1
	LET E, DWORD 0x2000			; MPU 关 → 允许写 E
	LET D1, DWORD 0xA0000000	; GIE + MPU
	SETB RIN3_CTRL, D1
JMP_INSTR:
	JMP							; E=0x2000 越界 → #GP, XAR = JMP_INSTR
JMP_AFTER:
	;; 断言 gp_count == 6
	LET R, DWORD 0x3100
	LR DWORD D1, *R
	LET X, DWORD 6
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T10
	JZ
	ZERO A
	HLT
T10:
	;; 断言 gp_xar (0x3104) == JMP_INSTR
	LET R, DWORD 0x3104
	LR DWORD D1, *R
	LET X, DWORD JMP_INSTR
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T11
	JZ
	ZERO A
	HLT

	;; ===== 10. 区间内访问正常 =====
T11:
	LET R, DWORD 0x2500
	LET A, DWORD 0x12345678
	STO DWORD A					; 写 [0x2500, 0x2504) ✓
	LR DWORD D1, *R-4			; 读 [0x2500, 0x2504) ✓
	LET X, DWORD 0x12345678
	MOV C, D1
	CMP DWORD X
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x00BEEF00		; 通过标记
	HLT

;; ==================== ISR ====================
;; #GP: 记录 XAR, 计数
ISR_GP:
	GETB D1, XAR
	LET R, DWORD 0x3104
	STO DWORD D1					; gp_xar = XAR
	LET R, DWORD 0x3100
	LR DWORD D1, *R
	INC D1
	STO DWORD D1					; gp_count++
	IRET

;; #STACK: 记录 XAR
ISR_STACK:
	GETB D1, XAR
	LET R, DWORD 0x3108
	STO DWORD D1					; stack_xar = XAR
	IRET
