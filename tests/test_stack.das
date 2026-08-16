;; ============================================================
;; test_stack.das - 栈与函数调用测试
;; 覆盖: SFA / RER / PUSHR / POPR / PUSH DWORD E (返回地址) / JMP
;;
;; 函数调用约定(演示):
;;   调用方:  LET E, DWORD 返回点; PUSH DWORD E; LET E, DWORD 函数; JMP
;;   函数:    SFA DWORD <局部大小>; ...函数体...; RER; JMP
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_stack.das
;;
;; 期望(寄存器转储):
;;   A  = 0x0000FAC7 (全部断言通过; 任一失败则 A=0x00000000)
;;   D1 = 0x0000004B (第二次调用 FUNC_SUM(50,25) 的返回值 75)
;;   S  = 0x00200000 (栈平衡: 返回起点)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- 设置栈底 ----
	LET S, DWORD 0x200000

	;; ---- 第一次调用 FUNC_SUM(100, 200) ----
	LET A, DWORD 100
	LET B, DWORD 200
	LET E, DWORD RET0
	PUSH DWORD E				; 压入返回地址
	LET E, DWORD FUNC_SUM
	JMP
RET0:
	;; D1 = 300
	;; 断言 D1 == 300
	LET C, DWORD 300
	CMP DWORD D1
	LET E, DWORD CALL2
	JZ
	ZERO A
	HLT

CALL2:
	;; ---- 第二次调用 FUNC_SUM(50, 25) ----
	LET A, DWORD 50
	LET B, DWORD 25
	LET E, DWORD RET1
	PUSH DWORD E
	LET E, DWORD FUNC_SUM
	JMP
RET1:
	;; D1 = 75

	;; ==================== 断言 ====================
	;; 断言 D1 == 75
	LET C, DWORD 75
	CMP DWORD D1
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x0000FAC7		; 通过标记
	HLT

;; ================ 函数: D1 = A + B ================
FUNC_SUM:
	;; 建立栈帧: F = S, S += 8 (8字节局部空间)
	SFA DWORD 8
	PUSHR						; 保存R
	MOV D1, A
	ADD DWORD D1, B				; D1 = A + B
	POPR						; 恢复R
	RER							; S = F; POP E (恢复返回地址)
	JMP							; 跳回调用方
