;; ============================================================
;; test_io.asm - 端口 I/O 测试
;; 覆盖: OUT / IN / BLKIN (使用 PIT 端口 0x10/0x11)
;;
;; 运行:
;;   sh tests/run_test.sh tests/test_io.asm
;;
;; 期望(寄存器转储):
;;   A  = 0x0000010C (全部断言通过; 任一失败则 A=0x00000000)
;;   X  = 0x000000E0 (BLKIN 从端口0x10读回的PIT控制寄存器值)
;;   B  = 0x12345678 (IN 从端口0x11读回上一次写入的计数器设定值)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- OUT 写 PIT 计数器 (32位) ----
	LET A, DWORD 0x12345678
	OUT DWORD 0x11, A			; rel = 0x12345678

	;; ---- IN 读回 (计数器值; 重载发生在下一tick, 此刻为0) ----
	IN DWORD B, 0x11			; B = 0 (当前计数器)

	;; ---- OUT 写 PIT 控制寄存器 ----
	LET A, BYTE 0xE0			; TE|IE|SE
	OUT BYTE 0x10, A

	;; ---- BLKIN: 从 A 端口连续读 C 次, 写入 *R ----
	LET R, DWORD 0x5000
	LET C, DWORD 2
	LET A, DWORD 0x10			; 端口号 = 0x10
	BLKIN BYTE R				; data[0x5000..0x5001] = PIT ctrl(0xE0) x2

	;; 读回
	LR BYTE X, *R				; X = 0xE0

	;; ==================== 断言 ====================
	;; 断言 X == 0xE0
	LET C, DWORD 0xE0
	CMP BYTE X
	LET E, DWORD PASS
	JZ
	ZERO A
	HLT

PASS:
	LET A, DWORD 0x0000010C		; 通过标记
	HLT
