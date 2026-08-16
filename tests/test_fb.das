;; ============================================================
;; test_fb.das - FB 帧缓冲设备 + Display 层测试
;; 程序在数据区构造 8x8 索引色帧缓冲与调色板，配置 FB 设备端口
;; 并触发刷新；Display 层按后端渲染（测试脚本用 ppm 后端检查文件）。
;;
;; 布局:
;;   调色板 @ 0x3200: 索引0=黑, 1=红, 2=绿, 3=蓝
;;   显存   @ 0x3000: 8x8, (0,0)=红, (4,4)=绿, (7,7)=蓝, 其余黑
;;   FB 端口: 0x20 控制(EN), 0x21 显存基址, 0x22 宽, 0x23 高,
;;            0x24 刷新, 0x25 调色板基址
;;
;; 运行:
;;   sh tests/run_display_test.sh
;;
;; 期望: A=0xFB00FB00 (程序正常完成; PPM 由 run_display_test.sh 检查)
;; ============================================================

	SECTION TEXT
	ORG 0

START:
	;; ---- 1. 调色板 (0x3200): 黑/红/绿/蓝 ----
	LET R, DWORD 0x3200
	ZERO A
	STO BYTE A					; 索引0 R
	STO BYTE A					;      G
	STO BYTE A					;      B
	LET A, BYTE 255
	STO BYTE A					; 索引1 R (红)
	ZERO A
	STO BYTE A					;      G
	STO BYTE A					;      B
	STO BYTE A					; 索引2 R (黑)
	LET A, BYTE 255
	STO BYTE A					;      G (绿)
	ZERO A
	STO BYTE A					;      B
	STO BYTE A					; 索引3 R
	STO BYTE A					;      G
	LET A, BYTE 255
	STO BYTE A					;      B (蓝)

	;; ---- 2. 显存清零 (0x3000 起 64 字节) ----
	LET R, DWORD 0x3000
	ZERO A
	LET C, DWORD 64
	REP STO BYTE A				; 重复 64 次写入并递增 R

	;; ---- 3. 画三个像素 ----
	LET R, DWORD 0x3000
	LET A, BYTE 1				; 红
	STO BYTE A					; (0,0)
	LET R, DWORD 0x3024			; 4*8+4 = 36
	LET A, BYTE 2				; 绿
	STO BYTE A					; (4,4)
	LET R, DWORD 0x303F			; 7*8+7 = 63
	LET A, BYTE 3				; 蓝
	STO BYTE A					; (7,7)

	;; ---- 4. 配置 FB 设备端口并刷新 ----
	LET A, DWORD 0x3000
	OUT DWORD 0x21, A			; 显存基址
	LET A, DWORD 0x3200
	OUT DWORD 0x25, A			; 调色板基址
	LET A, WORD 8
	OUT WORD 0x22, A			; 宽
	OUT WORD 0x23, A			; 高
	LET A, BYTE 0x01
	OUT BYTE 0x20, A			; 使能 EN
	OUT BYTE 0x24, A			; 刷新命令 → Display 层渲染

	;; ---- 5. 完成 ----
	LET A, DWORD 0xFB00FB00		; 通过标记
	HLT
