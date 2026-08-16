;; ============================================================
;; test_interrupt.asm - 周期模式 PIT 多次中断测试
;; 覆盖: ICT 配置 / SETB ICTB / SETB RIN3_CTRL / 周期PIT(SE=0)
;;       / IRQ0 派发 / ISR / IRET / 在ISR中关闭PIT
;;
;; 流程:
;;   1. data[0x1100] 写入 ICT 表项 0x10 (ISR_BASE=0x100, ISR_NMO=1)
;;   2. ICTB=0x1080, RIN3_CTRL: GIE=1, ISRB=0x10 (IRQ0 -> 中断号0x10)
;;   3. PIT 周期模式: TE|IE|TU=11(CPU周期), 计数器=3 -> 每3个tick触发一次IRQ0
;;   4. 主程序 HLT 等待; ISR 每次把 data[0] 计数+1
;;   5. 计数达到 3 后, ISR 关闭 PIT, 主程序不再被唤醒
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_interrupt.asm
;;
;; 期望(寄存器转储):
;;   A  = 0x00001E1E (主流程到达通过标记)
;;   X  = 0x00000003 (ISR 恰好执行 3 次)
;;   RIN2 = 0x00000000 (最后中断已被IRET清除)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- 配置 ICT 表项 0x10 (位于 ictb + 8*0x10 = 0x1100) ----
	LET R, DWORD 0x1100
	LET A, DWORD 0x100			; ISR 基址
	STO DWORD A
	LET A, DWORD 0x10			; byte4: ISR_NMO=1
	STO BYTE A

	;; ---- 设置 ICTB 与 RIN3_CTRL ----
	LET R, DWORD 0x1080
	SETB ICTB, R
	LET D1, DWORD 0x80100000	; GIE=1, ISRB=0x10
	SETB RIN3_CTRL, D1

	;; ---- PIT: 周期模式(SE=0), CPU周期(TU=11), 计数器=3 ----
	LET I, DWORD 0x7000			; I -> data[0x7000] 为 ISR 计数地址 (避开中断压栈区/ICT区)
	LET A, DWORD 3
	OUT BYTE 0x11, A			; 先写计数器
	LET A, BYTE 0xD8			; TE|IE|TU=11 (SE=0 -> 周期触发)
	OUT BYTE 0x10, A

	;; ---- 停机等待中断 ----
	HLT

	;; ---- 最终被唤醒后: 记录通过标记并停止 (PIT已被ISR关闭) ----
	LET A, DWORD 0x00001E1E
	HLT

	;; 填充到 0x100 (主程序+尾部共0x54字节) — ISR_BASE 指向 0x100
	RESB 0xAC

;; ==================== ISR @ 0x100 ====================
ISR_START:
	;; X = data[0] + 1
	LR DWORD X, *I
	INC X
	ST DWORD *I, X

	;; 若 X >= 3 则关闭 PIT, 否则直接返回
	LET C, DWORD 3
	CMP DWORD X					; C = 3 - X
	LET D2, DWORD 0
	LET E, DWORD DONE			; 跳转目标取自E
	JNG DWORD D2				; C <= 0 (X >= 3) -> 关闭PIT
	IRET

DONE:
	ZERO A
	OUT BYTE 0x10, A			; ctrl = 0: 关闭PIT
	IRET
