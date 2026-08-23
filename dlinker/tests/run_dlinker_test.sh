#!/bin/sh
# dlinker 自测：覆盖旧格式回退、ELF 重定位、-Ttext/-Tdata、--no-fallback。
set -e
cd "$(dirname "$0")/.."

DASM=../dasm/dasm
DLINKER=./dlinker
T=$(mktemp -d /tmp/dlinker_test.XXXXXX)
trap 'rm -rf "$T"' EXIT
ok=0; fail=0

check() { # desc, actual, expected
    if [ "$2" = "$3" ]; then
        echo "PASS: $1"
        ok=$((ok+1))
    else
        echo "FAIL: $1 (actual=$2 expected=$3)"
        fail=$((fail+1))
    fi
}

make >/dev/null

# ---- 1. 旧格式回退：dasm 生成的无重定位 .o ----
cat > "$T/a.asm" <<'ASM'
SECTION TEXT
ORG 0
_start:
LET S, DWORD 0x300000
LET E, DWORD _start
JMP
SECTION DATA
ORG 0
v1:
DD 0, 0x12345678
ASM
cat > "$T/b.asm" <<'ASM'
SECTION TEXT
ORG 0
func_b:
LET A, DWORD 0x2222
HLT
SECTION DATA
ORG 0
v2:
DD 0, 0x9abcdef0
ASM
$DASM -m elf "$T/a.asm" "$T/a.o" >/dev/null
$DASM -m elf "$T/b.asm" "$T/b.o" >/dev/null
$DLINKER -o "$T/out" "$T/a.o" "$T/b.o" >/dev/null
check "旧格式链接 code 大小" "$(wc -c < "$T/out_code.bin")" "25"
check "旧格式链接 data 大小" "$(wc -c < "$T/out_data.bin")" "8"

# ---- 2. ELF 重定位 + 基址选项 ----
if command -v as >/dev/null 2>&1; then
    cat > "$T/r1.s" <<'ASM'
    .data
    .globl shared
shared:
    .long 0
    .text
    .globl f1
f1:
    movl $shared, %eax
    ret
ASM
    cat > "$T/r2.s" <<'ASM'
    .text
    .globl f2
f2:
    movl $shared, %eax
    ret
ASM
    as --32 -o "$T/r1.o" "$T/r1.s"
    as --32 -o "$T/r2.o" "$T/r2.s"
    $DLINKER -Ttext 0x100 -Tdata 0x200 -o "$T/rout" "$T/r1.o" "$T/r2.o" >/dev/null
    # r1 的 mov imm 应指向 shared = 0x200；r2 同理
    H=$(od -A n -t x1 "$T/rout_code.bin" | tr -d ' \n')
    check "重定位后代码内容" "$H" "b800020000c3b800020000c3"

    # .data/.rodata/.bss 混合段 + -Tdata
    cat > "$T/mix.s" <<'ASM'
    .data
    .globl d
d:
    .long 0
    .section .rodata
    .globl r
r:
    .long 0
    .bss
    .globl b
b:
    .zero 4
    .text
    .globl f
f:
    movl $d, %eax
    movl $r, %eax
    movl $b, %eax
    ret
ASM
    as --32 -o "$T/mix.o" "$T/mix.s"
    $DLINKER -Tdata 0x200 -o "$T/mixout" "$T/mix.o" >/dev/null
    HM=$(od -A n -t x1 "$T/mixout_code.bin" | tr -d ' \n')
    check "混合段重定位内容" "$HM" "b800020000b808020000b804020000c3"
fi

# ---- 3. --no-fallback 拒绝旧格式 ----
# 注：dasm 现在会对本地标号引用也生成重定位，因此构造一个真正
# 不含任何标号引用的纯数据/纯立即数对象来测试 --no-fallback。
cat > "$T/noreloc.asm" <<'ASM'
SECTION TEXT
ORG 0
    LET A, DWORD 0x1234
    HLT
SECTION DATA
ORG 0
    DD 0, 0x55
ASM
$DASM -m elf "$T/noreloc.asm" "$T/noreloc.o" >/dev/null
if $DLINKER --no-fallback -o "$T/nf" "$T/noreloc.o" >/dev/null 2>&1; then
    echo "FAIL: --no-fallback 应拒绝无重定位表输入"
    fail=$((fail+1))
else
    echo "PASS: --no-fallback 拒绝旧格式"
    ok=$((ok+1))
fi

echo "========================"
echo "dlinker 测试: $ok 通过, $fail 失败"
[ "$fail" -eq 0 ] || exit 1
