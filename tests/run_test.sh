#!/bin/sh
# 汇编并运行单个 .asm 测试, 自动检查通过标记
# 用法: sh tests/run_test.sh tests/test_arith.asm
set -e
cd "$(dirname "$0")/.."

DAS="$1"
if [ -z "$DAS" ]; then
	echo "用法: sh tests/run_test.sh <test.asm>"
	exit 1
fi
BASE=$(basename "$DAS" .asm)
CODE="/tmp/${BASE}_code.bin"
DATA="/tmp/${BASE}_data.bin"
OUT="/tmp/${BASE}_out.txt"

echo "== 汇编 $DAS =="
(cd tools/dasm && ./dasm "../../$DAS" "$CODE" "$DATA" >/dev/null) || { echo "汇编失败"; exit 1; }

echo "== 运行 (SIGINT超时2秒后转储寄存器) =="
timeout -s INT 2 ./build/bin/doctor_sim -f code "$CODE" data "$DATA" > "$OUT" 2>&1 || true
tail -12 "$OUT"
printf '\n'	# 防止 UART 等无换行输出粘住后续断言行

# 自动断言: 文件头注释 ";;   A  = 0x..." 的期望值 vs 最终转储的 A
# (转储中 A 位于 E 行内, 格式 "E =0x.. S =0x.. T =0x.. A =0x..")
EXPECT=$(grep -oE 'A[[:space:]]*=[[:space:]]*0x[0-9A-Fa-f]+' "$DAS" | head -1 | grep -oE '0x[0-9A-Fa-f]+')
if [ -n "$EXPECT" ]; then
	ACTUAL=$(grep -oE 'A =0x[0-9A-Fa-f]+' "$OUT" | tail -1 | grep -oE '0x[0-9A-Fa-f]+')
	if [ "$ACTUAL" = "$EXPECT" ]; then
		echo "PASS: A=$ACTUAL (期望 $EXPECT)"
	else
		echo "FAIL: A=$ACTUAL (期望 $EXPECT)"
		exit 1
	fi
fi
