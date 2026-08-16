#!/bin/sh
# ============================================================
# run_dasm_test.sh - dasm 语法修复 (B1.1-B1.7) 字节级测试
# 覆盖: *reg+N 编码 / PUSH P / NZ 拼接 / 表达式 / ORG 填充 /
#       DB-DW-DD 地址定位 / RER 2字节 / 错误行号 / 尺寸检查
# 用法: sh tests/run_dasm_test.sh
# ============================================================
set -e
cd "$(dirname "$0")/.."

DASM=tools/dasm/dasm
T=$(mktemp -d /tmp/dasm_test.XXXXXX)
trap 'rm -rf "$T"' EXIT
ok=0; fail=0

hexstr() { od -A n -t x1 "$1" | tr -d ' \n'; }

assert_substr() { # 描述, 十六进制串, 期望子串
	if echo "$2" | grep -q "$3"; then
		echo "PASS: $1"
		ok=$((ok+1))
	else
		echo "FAIL: $1"
		echo "      期望包含: $3"
		echo "      实际:     $2"
		fail=$((fail+1))
	fi
}

assert_err_line() { # 描述, 文件, 期望行号
	out=$($DASM "$2" 2>&1 || true)
	if echo "$out" | grep -q "行 $3:"; then
		echo "PASS: $1 (报错行 $3)"
		ok=$((ok+1))
	else
		echo "FAIL: $1 (期望报错 行 $3)"
		echo "$out" | grep "行" | head -3
		fail=$((fail+1))
	fi
}

# ---- 1. LR/ST 指针偏移编码 (*reg+N) ----
cat > "$T/f1.asm" <<'EOF'
	SECTION TEXT
	ORG 0
	LR DWORD A, *R+4
	ST BYTE *I-2, B
	LR WORD C, *E+0x10
	HLT
EOF
$DASM "$T/f1.asm" "$T/f1.code" "$T/f1.data" >/dev/null 2>&1
H=$(hexstr "$T/f1.code")
assert_substr "LR DWORD A, *R+4 (imm=4)" "$H" "65030904000000"
assert_substr "ST BYTE *I-2, B (BYTE负偏移 0xFE)" "$H" "2204b1fe"
assert_substr "LR WORD C, *E+0x10 (WORD偏移)" "$H" "4303281000"

# ---- 2. PUSH P / pushp 编码 (0x3B) ----
cat > "$T/f2.asm" <<'EOF'
	SECTION TEXT
	ORG 0
	PUSH DWORD P
	pushp
	PUSH BYTE P
	HLT
EOF
$DASM "$T/f2.asm" "$T/f2.code" "$T/f2.data" >/dev/null 2>&1
H=$(hexstr "$T/f2.code")
assert_substr "PUSH DWORD P -> 61 3B EE" "$H" "613bee"
assert_substr "pushp -> 61 3B EE (缺省DWORD)" "$H" "613bee"
assert_substr "PUSH BYTE P -> 21 3B EE" "$H" "213bee"

# ---- 3. NZ 拼接形式 (ADDNZ) ----
cat > "$T/f3.asm" <<'EOF'
	SECTION TEXT
	ORG 0
	LET A, DWORD 0xDEADBEEF
	ADDNZ BYTE A, 0x21
	XORNZ BYTE A, B
	HLT
EOF
$DASM "$T/f3.asm" "$T/f3.code" "$T/f3.data" >/dev/null 2>&1
H=$(hexstr "$T/f3.code")
assert_substr "ADDNZ BYTE A, 0x21" "$H" "32060f21"
assert_substr "XORNZ BYTE A, B" "$H" "311301"

# ---- 4. 表达式: 标号偏移 与 $-based RESB ----
cat > "$T/f4.asm" <<'EOF'
	SECTION TEXT
	ORG 0x100
START:
	LET A, DWORD START+4
	RESB 0x110 - $
LAB:
	LET B, DWORD LAB-START
	HLT
EOF
$DASM "$T/f4.asm" "$T/f4.code" "$T/f4.data" >/dev/null 2>&1
H=$(hexstr "$T/f4.code")
assert_substr "LET A, DWORD START+4 (0x104)" "$H" "65000f04010000"
assert_substr "LET B, DWORD LAB-START (0x10)" "$H" "65001f10000000"
SZ=$(wc -c < "$T/f4.code")
if [ "$SZ" -eq 281 ]; then
	echo "PASS: ORG 0x100 + RESB 0x110-\$ 总长 281 字节"
	ok=$((ok+1))
else
	echo "FAIL: 期望 281 字节, 实际 $SZ"
	fail=$((fail+1))
fi

# ---- 5. ORG 填充输出缓冲 (B1.5) ----
SZ=$(wc -c < "$T/f4.code")
if [ "$SZ" -ge 256 ]; then
	echo "PASS: ORG 0x100 填充输出缓冲 (文件 >= 0x100 字节)"
	ok=$((ok+1))
else
	echo "FAIL: ORG 未填充 (文件 $SZ 字节)"
	fail=$((fail+1))
fi
# 0x100 之前的字节应为 0x00
FIRST=$(head -c 256 "$T/f4.code" | tr -d '\0' | wc -c)
if [ "$FIRST" -eq 0 ]; then
	echo "PASS: ORG 填充区全为 0x00"
	ok=$((ok+1))
else
	echo "FAIL: ORG 填充区含非零字节"
	fail=$((fail+1))
fi

# ---- 6. DB/DW/DD 地址定位 (B1.6) ----
cat > "$T/f5.asm" <<'EOF'
	SECTION DATA
	ORG 0x200
	DB 0x210, 0xAA
	DW 0x220, 0x1234
MSG:
	DB 0x228, "AB"
EOF
$DASM "$T/f5.asm" "$T/f5.code" "$T/f5.data" >/dev/null 2>&1
H=$(hexstr "$T/f5.data")
assert_substr "DB 0x210, 0xAA" "$H" "aa"
assert_substr "DW 0x220, 0x1234 (LE)" "$H" "3412"
assert_substr "DB 0x228, \"AB\"" "$H" "4142"
# 符号地址 MSG 应为 0x222（DW 之后）
$DASM "$T/f5.asm" "$T/f5.code" "$T/f5.data" 2>&1 | grep -q "MSG.*0x222" \
	&& { echo "PASS: MSG 地址 = 0x222"; ok=$((ok+1)); } \
	|| { echo "FAIL: MSG 地址 != 0x222"; fail=$((fail+1)); }

# ---- 7. RER 为 2 字节指令 (encoder 修复) ----
cat > "$T/f6.asm" <<'EOF'
	SECTION TEXT
	ORG 0
FUNC:
	RER
NEXT:
	LET E, DWORD NEXT
	HLT
EOF
$DASM "$T/f6.asm" "$T/f6.code" "$T/f6.data" >/dev/null 2>&1
H=$(hexstr "$T/f6.code")
assert_substr "RER 编码为 00 19 (2字节)" "$H" "0019"
$DASM "$T/f6.asm" "$T/f6.code" "$T/f6.data" 2>&1 | grep -q "NEXT.*0x2" \
	&& { echo "PASS: RER 后标号 NEXT = 0x2"; ok=$((ok+1)); } \
	|| { echo "FAIL: RER 后标号 NEXT != 0x2"; fail=$((fail+1)); }

# ---- 8. 错误行号为源文件行号 (B1.7) ----
cat > "$T/f7.asm" <<'EOF'


; 注释行
; 更多注释


	ZERO A
	BADINSTR B
EOF
assert_err_line "错误行号 = 源文件行号 (BADINSTR 在第8行)" "$T/f7.asm" 8

# ---- 9. 尺寸检查: 需要尺寸的指令缺尺寸报错 ----
cat > "$T/f8.asm" <<'EOF'
	SECTION TEXT
	ORG 0
	PUSH 0x10
	HLT
EOF
out=$($DASM "$T/f8.asm" 2>&1 || true)
if echo "$out" | grep -q "需要显式尺寸"; then
	echo "PASS: PUSH 缺尺寸报错"
	ok=$((ok+1))
else
	echo "FAIL: PUSH 缺尺寸未报错"
	fail=$((fail+1))
fi

# ---- 10. 解引用校验: INC *R 应报错 ----
cat > "$T/f9.asm" <<'EOF'
	SECTION TEXT
	ORG 0
	INC *R
	HLT
EOF
out=$($DASM "$T/f9.asm" 2>&1 || true)
if echo "$out" | grep -q "不支持 \* 解引用"; then
	echo "PASS: INC *R 报错 (仅 LR/ST/POR 允许解引用)"
	ok=$((ok+1))
else
	echo "FAIL: INC *R 未报错"
	fail=$((fail+1))
fi

echo "========================"
echo "dasm 语法测试: $ok 通过, $fail 失败"
[ "$fail" -eq 0 ] || exit 1
