;; sample1.asm - DOCTOR ASM的示例程序

	;; 设置栈底位于0x200000处
	LET S, DWORD 0x200000
	;; 设置中断控制表位于数据的开头
	PUSH DWORD 0x000000
	PUSH BYTE 0000_0000B			;; 关闭中断
	POP RIN3
	;; 填写中断控制表
	LET E, DWORD DEFAULT_INTR_END
	JMP					;; 跳过默认的中断处理程序

DEFAULT_INTR:
	iret

DEFAULT_INTR_END:
	ZERO R					;; R位于中断控制表开头处
	BLKS DWORD 256, DEFAULT_INTR		;; 利用块操作，直接写入默认中断
	;; 设置8259A兼容芯片
	PUSH DWORD 0x11080401			;; 11/08/04/01 分别是ICW1~4
	POP RIN1
	PUSH DWORD 0x11700201
	POP RIN2				;; 同上
	;; 开启中断
	PUSH RIN3
	POP BYTE A
	PUSH BYTE 1000_0000B			;; 开启中断
	POP RIN3
	;; 所有寄存器的初始化
	ZERO A
	ZERO B
	ZERO C
	ZERO D1
	ZERO D2
	ZERO T
	LET F, DWORD 0X200000			;; F指向栈底
	;; 跳转至模拟器硬编码的程序开始地址
	LET E, DWORD 0x000200
	JMP
