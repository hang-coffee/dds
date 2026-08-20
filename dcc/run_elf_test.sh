#!/bin/sh
# dcc -m elf 冒烟测试：验证能直接生成 ELF32 relocatable .o
set -e
cd "$(dirname "$0")"

make >/dev/null

T=$(mktemp -d /tmp/dcc_elf_test.XXXXXX)
trap 'rm -rf "$T"' EXIT

cat > "$T/hello.c" <<'C'
int add(int a, int b) { return a + b; }
int main(void) { return add(1, 2); }
C

# 1. 默认输出 .o
./dcc -m elf "$T/hello.c" >/dev/null
test -s "$T/hello.o" || { echo "FAIL: 未生成 $T/hello.o"; exit 1; }
MAGIC=$(head -c 4 "$T/hello.o" | od -An -tx1 | tr -d ' \n')
[ "$MAGIC" = "7f454c46" ] || { echo "FAIL: 不是 ELF 文件 (magic=$MAGIC)"; exit 1; }
echo "PASS: dcc -m elf 默认输出 .o"

# 2. 显式输出 .o
./dcc -m elf "$T/hello.c" "$T/out.o" >/dev/null
test -s "$T/out.o" || { echo "FAIL: 未生成 $T/out.o"; exit 1; }
echo "PASS: dcc -m elf 显式输出 .o"

# 3. 多文件合并后输出 ELF
cat > "$T/lib.c" <<'C'
int square(int x) { return x * x; }
C
cat > "$T/main.c" <<'C'
int square(int x);
int main(void) { return square(6); }
C
./dcc -m elf "$T/main.c" "$T/lib.c" "$T/all.o" >/dev/null
test -s "$T/all.o" || { echo "FAIL: 多文件 ELF 未生成"; exit 1; }
echo "PASS: dcc -m elf 多文件输出 .o"

echo "----"
echo "dcc -m elf 测试全部通过"
