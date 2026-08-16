;; sample.asm - 演示 DOCTOR 汇编器（dasm）用法
;;
;; 演示: 指令 / 标号 / 跳转 / DATA 段 / ORG 填充 / DB-DW-DD 地址定位 / 表达式

	SECTION TEXT
	ORG 0x200				; 代码从 0x200 开始（输出缓冲自动填充到 0x200）
MAIN:
	LET A, DWORD 0x0BADF00D
	ADD DWORD A, B
	LET E, DWORD LOOP
	JMP
	NOP
LOOP:
	CSI
	LET E, DWORD EXIT
	JZ
	LET E, DWORD LOOP
	JMP
EXIT:
	HLT

	SECTION DATA
	ORG 0x2000
MSG:
	DB 0x2000, "Hello, World!"		; 数据定位到 0x2000（之前自动填充 0x00）
	DW 0x2010, 0x1234				; 定位到 0x2010
	DD 0x2020, 0x56789ABC			; 定位到 0x2020
	DQ 0x2030, 0x1122334455667788	; 定位到 0x2030
	RESB 0x2040 - $				; 表达式: 填充至 0x2040（$ = 当前位置）
TAIL:
	DB 0x2040, 0xFF
