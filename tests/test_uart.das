;; ============================================================
;; test_uart.das - UART 设备端到端测试
;; 覆盖: 环回收发 / TX 状态与溢出 / RX 溢出 / 接收中断(IRQ1→ISR)
;;
;; ICT: 0x11 (ISRB=0x10, IRQ1) @ 0x1088 = ISR_UART; KSP 不需要(SS=0)
;; 标志: 0x3100 isr_flag
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_uart.das
;;
;; 期望(寄存器转储):
;;   A  = 0x0A115E5E (全部断言通过; 任一失败则 A=0x00000000)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- ICT[0x11] @ 0x1088 = ISR_UART ----
	LET R, DWORD 0x1088
	LET A, DWORD ISR_UART
	STO DWORD A
	LET A, DWORD 0
	STO BYTE A
	LET R, DWORD 0x1000
	SETB ICTB, R
	LET S, DWORD 0x3000
	;; GIE=1, ISRB=0x10 (IRQ1 → 中断号 0x11)
	LET D1, DWORD 0x80100000
	SETB RIN3_CTRL, D1

	;; ===== 1. 环回收发: 写 'A' 读回 =====
	LET A, DWORD 0x05			; EN|LOOP, TX_DELAY=0
	OUT DWORD 0x18, A
	LET A, BYTE 0x41			; 'A'
	OUT BYTE 0x16, A			; 发送（环回进入接收 FIFO）
	;; 断言 RX_READY (0x15 bit0)
	IN WORD D1, 0x15
	LET X, DWORD 1
	AND DWORD D1, X
	LET C, DWORD 1
	CMP DWORD D1
	LET E, DWORD T1
	JZ
	ZERO A
	HLT
T1:
	;; 断言读回 'A'
	IN BYTE D1, 0x14
	LET X, DWORD 0x41
	MOV C, D1
	CMP DWORD X
	LET E, DWORD T2
	JZ
	ZERO A
	HLT

	;; ===== 2. TX 状态与溢出 (TX_DELAY=12, 保证断言序列仍在发送窗口内) =====
T2:
	LET A, DWORD 0x65			; EN|LOOP|DELAY(12<<3=0x60) = 0x65
	OUT DWORD 0x18, A
	LET A, BYTE 0x44			; 'D'
	OUT BYTE 0x16, A			; 开始发送 → TX_READY=0
	;; 断言 TX_READY (0x17 bit0) == 0
	IN WORD D1, 0x17
	LET X, DWORD 1
	AND DWORD D1, X
	LET C, DWORD 0
	CMP DWORD D1
	LET E, DWORD T3
	JZ
	ZERO A
	HLT
T3:
	;; 忙时写入 → 丢弃 + TX_OVERFLOW
	LET A, BYTE 0x45			; 'E'
	OUT BYTE 0x16, A
	IN WORD D1, 0x17
	LET X, DWORD 2
	AND DWORD D1, X
	LET C, DWORD 2
	CMP DWORD D1
	LET E, DWORD T4
	JZ
	ZERO A
	HLT
T4:
	;; 等待发送完成 (3 ticks)
	NOP
	NOP
	NOP
	IN WORD D1, 0x17
	LET X, DWORD 1
	AND DWORD D1, X
	LET C, DWORD 1
	CMP DWORD D1				; TX_READY 恢复
	LET E, DWORD T5
	JZ
	ZERO A
	HLT
T5:
	;; 清除 TX_OVERFLOW (W1C 写 0x17)
	LET A, WORD 2
	OUT WORD 0x17, A
	IN WORD D1, 0x17
	LET X, DWORD 2
	AND DWORD D1, X
	LET C, DWORD 0
	CMP DWORD D1
	LET E, DWORD T6
	JZ
	ZERO A
	HLT

	;; ===== 3. RX 溢出 (TX_DELAY=0, 连续 17 字节) =====
T6:
	LET A, DWORD 0x05			; EN|LOOP, delay=0
	OUT DWORD 0x18, A
	LET C, DWORD 17
	LET A, BYTE 0x55			; 'U'
	REP OUT BYTE 0x16, A		; 17 次发送 → FIFO(16) 满 + 溢出
	;; 断言 RX_OVERFLOW (0x15 bit1)
	IN WORD D1, 0x15
	LET X, DWORD 2
	AND DWORD D1, X
	LET C, DWORD 2
	CMP DWORD D1
	LET E, DWORD T6B
	JZ
	ZERO A
	HLT
T6B:
	;; 清空 FIFO (16 字节)
	LET C, DWORD 16
	REP IN BYTE D1, 0x14
	;; 断言 RX_READY == 0 (FIFO 空)
	IN WORD D1, 0x15
	LET X, DWORD 1
	AND DWORD D1, X
	LET C, DWORD 0
	CMP DWORD D1
	LET E, DWORD T7
	JZ
	ZERO A
	HLT

	;; ===== 4. 接收中断: 环回 'Z' → IRQ1 → ISR =====
T7:
	LET A, DWORD 0x07			; EN|IE|LOOP
	OUT DWORD 0x18, A
	LET A, BYTE 0x5A			; 'Z'
	OUT BYTE 0x16, A			; → FIFO + irq_set(IRQ1)
	;; 忙等 isr_flag == 1（中断在指令边界自动派发，ISR 读回 'Z' 并置标志）
UART_WAIT:
	LET R, DWORD 0x3100
	LR DWORD D1, *R
	LET C, DWORD 1
	CMP DWORD D1
	LET E, DWORD PASS
	JZ							; flag==1 → PASS
	LET E, DWORD UART_WAIT
	JMP

PASS:
	LET A, DWORD 0x0A115E5E		; 通过标记
	HLT

;; ==================== ISR ====================
ISR_UART:
	IN BYTE D1, 0x14			; 读 'Z'
	LET X, DWORD 0x5A
	MOV C, D1
	CMP DWORD X
	LET E, DWORD ISR_OK
	JZ
	LET R, DWORD 0x3100
	ZERO A
	STO DWORD A					; isr_flag = 0 (失败)
	IRET
ISR_OK:
	LET R, DWORD 0x3100
	LET A, DWORD 1
	STO DWORD A					; isr_flag = 1
	IRET
