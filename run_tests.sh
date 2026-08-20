#!/bin/sh
# ============================================================
# run_tests.sh - DOCTOR Development Suite 一键测试
#
# 运行所有子项目的自动化测试：
#   - 构建全部工具
#   - dasm 单元测试
#   - doctor-sim 汇编/模拟器测试（含所有 test_*.asm）
#   - dcc 端到端测试
#   - dlinker 链接器测试
#   - dcc ELF 冒烟测试
#
# 用法:
#   ./run_tests.sh
# ============================================================

cd "$(dirname "$0")"

PASS=0
FAIL=0

run_test() {
    desc="$1"
    shift
    echo "===== $desc ====="
    if "$@"; then
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc"
        FAIL=$((FAIL + 1))
    fi
    echo
}

run_test "构建全部子项目" make all
run_test "dasm 单元测试" make -C dasm test
run_test "doctor-sim dasm 语法/字节级测试" sh doctor-sim/tests/run_dasm_test.sh
run_test "doctor-sim PIT/IRQ 端到端测试" sh doctor-sim/tests/run_irq_test.sh
run_test "doctor-sim Display/FB 测试" sh doctor-sim/tests/run_display_test.sh

for t in doctor-sim/tests/test_*.asm; do
    run_test "doctor-sim 汇编测试: $t" sh doctor-sim/tests/run_test.sh "$t"
done

run_test "dcc 端到端测试" make -C dcc test
run_test "dcc ELF 冒烟测试" sh dcc/run_elf_test.sh
run_test "dlinker 测试" make -C dlinker test

echo "================================"
echo "一键测试结果: PASS=$PASS FAIL=$FAIL"
echo "================================"

[ "$FAIL" -eq 0 ]
