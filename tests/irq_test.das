;; irq_test.das - PIT→IRQ0→ISR→IRET 端到端测试 (由dasm汇编)
;;
;; 流程:
;;   1. 在数据区 0x1080 写入 ICT 第 0x10 项 (ISR_BASE=0x100, byte4=0x10 即 ISR_NMO=1)
;;   2. SETB ICTB, R          -> RIN3高32位 = 0x1080
;;   3. SETB RIN3_CTRL, D1    -> GIE=1, ISRB=0x10 (IRQ0 -> 中断号0x10)
;;   4. PIT: 单次触发(SE=1), 计数器=10, CPU周期模式
;;   5. HLT 停机等待 -> PIT归零 -> IRQ0 -> 主循环派发中断 -> 跳转ISR
;;   6. ISR: X = data[0x2000]++, IRET
;;   7. IRET 返回 HLT 之后: B = 0xCAFEBABE, 再次 HLT
;;
;; 验证(寄存器转储): X=0x00000001 (ISR执行了一次), B=0xCAFEBABE (IRET正确返回)

	SECTION TEXT
	ORG 0

START:
	;; 1. 填写 ICT 第 0x10 项 (位于 ictb + 8*0x10 = 0x1080 + 0x80 = 0x1100):
	;;    ISR_BASE=0x100, byte4=0x10(ISR_NMO=1)
	LET R, DWORD 0x1100
	LET A, DWORD 0x100
	STO DWORD A
	LET A, DWORD 0x10
	STO BYTE A

	;; 2. 设置 ICTB 与 RIN3_CTRL
	LET R, DWORD 0x1080
	SETB ICTB, R
	LET I, DWORD 0x2000			;; ISR计数地址
	LET D1, DWORD 0x80100000	;; GIE=1, ISRB=0x10
	SETB RIN3_CTRL, D1

	;; 3. 配置PIT: TE|IE|SE=1(单次), TU=11(CPU周期模式), 计数器=10
	LET A, DWORD 10
	OUT BYTE 0x11, A			;; 先写计数器
	LET A, BYTE 0xF8			;; TE|IE|SE|TU=11
	OUT BYTE 0x10, A			;; 再使能计时器

	;; 4. 停机等待中断
	HLT

	;; 5. IRET 返回后执行到这里
	LET B, DWORD 0xCAFEBABE
	HLT

	;; 6. 填充到 0x100 (主程序共0x54字节, 填充0xAC)
	RESB 0xAC

;; ==================== ISR @ 0x100 ====================
ISR_START:
	LR DWORD X, *I			;; X = data[0x2000]
	INC X
	ST DWORD *I, X			;; data[0x2000] = X
	IRET
