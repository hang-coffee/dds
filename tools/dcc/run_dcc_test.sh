#!/bin/sh
# run_dcc_test.sh - dcc 端到端测试：C → dasm → 汇编 → 模拟器
#
# 每个测试 .c 文件头部注释声明期望的 A 寄存器值，格式：
#   /* expect A = 0x45 */
# 脚本编译、汇编、运行，并断言最终转储的 A 与期望一致。
#
# 用法: sh run_dcc_test.sh <test.c>   （不带参数则运行全部 test/*.c）
set -e
cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)		# 项目根目录（doctor-emu/）

if [ $# -ge 1 ]; then
	FILES="$@"
else
	# 只收集带 "expect A =" 注释的测试主文件（辅助源文件如 lib_math.c 自动排除）
	FILES=""
	for f in test/*.c; do
		if grep -qE 'expect A = 0x[0-9A-Fa-f]+' "$f" 2>/dev/null; then
			FILES="$FILES $f"
		fi
	done
fi

PASS=0
FAIL=0
for SRC in $FILES; do
	BASE=$(basename "$SRC" .c)
	ASM="/tmp/dcc_${BASE}.asm"
	CODE="/tmp/dcc_${BASE}.code"
	DATA="/tmp/dcc_${BASE}.data"
	OUT="/tmp/dcc_${BASE}.out"

	# 1. dcc 编译（多文件：测试文件头注释 "sources: a.c b.c" 声明附加源文件）
	EXTRA_SRC=$(grep -oE 'sources: [^*]+' "$SRC" | head -1 | sed 's/sources:[[:space:]]*//')
	IN_FILES="$SRC"
	for x in $EXTRA_SRC; do
		IN_FILES="$IN_FILES $(dirname "$SRC")/$x"	# 附加源文件相对测试目录
	done
	if ! ./dcc $IN_FILES "$ASM" >/dev/null 2>&1; then
		echo "FAIL $SRC: dcc 编译失败"
		FAIL=$((FAIL+1))
		continue
	fi
	# 2. dasm 汇编（输出 code 与 data 到 /tmp）
	if ! ../dasm/dasm "$ASM" "$CODE" "$DATA" >/dev/null 2>&1; then
		echo "FAIL $SRC: dasm 汇编失败"
		FAIL=$((FAIL+1))
		continue
	fi
	# 3. 模拟器运行（SIGINT 2 秒后转储；code+data 都加载）
	(cd "$ROOT" && timeout -s INT 2 ./build/bin/doctor_sim -f code "$CODE" data "$DATA" > "$OUT" 2>&1) || true

	# 4. 断言 A（期望值在源文件头注释，如 "expect A = 0x45"）
	EXPECT=$(grep -oE 'expect A = 0x[0-9A-Fa-f]+' "$SRC" | head -1 | grep -oE '0x[0-9A-Fa-f]+' || true)
	if [ -z "$EXPECT" ]; then
		echo "SKIP $SRC: 源文件头缺少 'expect A = 0x..' 注释"
		continue
	fi
	ACTUAL=$(grep -oE 'A =0x[0-9A-Fa-f]+' "$OUT" | tail -1 | grep -oE '0x[0-9A-Fa-f]+' || true)
	# 数值比较（忽略前导零）
	if [ -n "$ACTUAL" ] && [ $((ACTUAL)) -eq $((EXPECT)) ]; then
		echo "PASS $SRC: A=$ACTUAL (期望 $EXPECT)"
		PASS=$((PASS+1))
	else
		echo "FAIL $SRC: A=$ACTUAL (期望 $EXPECT)"
		FAIL=$((FAIL+1))
	fi
done

echo "----"
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
