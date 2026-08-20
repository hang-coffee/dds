#!/bin/sh
# ============================================================
# run_display_test.sh - Display 层 + FB 设备端到端测试
# 汇编 test_fb.asm，用 ppm 后端运行模拟器，检查导出的 PPM 图像
# 期望: 8x8 帧缓冲, (0,0)=红, (4,4)=绿, (7,7)=蓝, 其余黑
# 用法: sh tests/run_display_test.sh
# ============================================================
set -e
cd "$(dirname "$0")/.."

DASM=../dasm/dasm
T=$(mktemp -d /tmp/dsp_test.XXXXXX)
trap 'rm -rf "$T"' EXIT
ok=0; fail=0

check() { # 描述, 实际, 期望
	if [ "$2" = "$3" ]; then
		echo "PASS: $1"
		ok=$((ok+1))
	else
		echo "FAIL: $1 (实际 $2, 期望 $3)"
		fail=$((fail+1))
	fi
}

echo "== 1. 汇编 test_fb.asm =="
$DASM tests/test_fb.asm "$T/code.bin" "$T/data.bin" >/dev/null || { echo "汇编失败"; exit 1; }

echo "== 2. 运行模拟器 (ppm 后端, 8x8) =="
timeout -s INT 3 ./build/bin/doctor-sim \
	--display ppm --display-file "$T/fb.ppm" --display-size 8x8 \
	-f code "$T/code.bin" data "$T/data.bin" > "$T/out.txt" 2>&1 || true

echo "== 3. 检查 PPM 文件 =="
if [ ! -f "$T/fb.ppm" ]; then
	echo "FAIL: 未生成 $T/fb.ppm"
	exit 1
fi
# 头部: "P6\n8 8\n255\n"
HEAD=$(head -c 11 "$T/fb.ppm" | od -A n -t x1 | tr -d ' \n')
check "PPM 头部 (P6/8x8/255)" "$HEAD" "50360a3820380a3235350a"

# 像素提取: 头部 11 字节后每像素 3 字节, 行主序
pix() { # 像素偏移(0..63) -> "RRGGBB"
	dd if="$T/fb.ppm" bs=1 skip=$((11 + $1*3)) count=3 2>/dev/null | od -A n -t x1 | tr -d ' \n'
}
check "像素(0,0) = 红"    "$(pix 0)"   "ff0000"
check "像素(4,4) = 绿"    "$(pix 36)"  "00ff00"
check "像素(7,7) = 蓝"    "$(pix 63)"  "0000ff"
check "像素(1,1) = 黑"    "$(pix 9)"   "000000"

echo "== 4. 检查模拟器运行到完成 =="
if grep -q "A =0xFB00FB00" "$T/out.txt"; then
	echo "PASS: 程序正常完成 (A=0xFB00FB00)"
	ok=$((ok+1))
else
	echo "FAIL: 未看到完成标记 A=0xFB00FB00"
	grep -E "FATAL|A =" "$T/out.txt" | tail -3
	fail=$((fail+1))
fi
if grep -q "FATAL" "$T/out.txt"; then
	echo "FAIL: 出现 FATAL"
	fail=$((fail+1))
fi

echo "========================"
echo "Display 测试: $ok 通过, $fail 失败"
[ "$fail" -eq 0 ] || exit 1
