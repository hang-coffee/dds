#!/bin/sh
# 一键运行 PIT→IRQ0→ISR→IRET 端到端测试
# 用法: sh tests/run_irq_test.sh
set -e
cd "$(dirname "$0")/.."

echo "== 1. 编译模拟器 =="
make

echo "== 2. 用 dasm 汇编测试程序 =="
(cd tools/dasm && ./dasm ../tests/irq_test.das ../tests/bin/irq_test_code.bin ../tests/bin/irq_test_data.bin | tail -1)

echo "== 3. 运行模拟器 (SIGINT超时3秒后转储寄存器) =="
timeout -s INT 3 ./build/bin/doctor_sim \
	-f code tests/bin/irq_test_code.bin data tests/bin/irq_test_data.bin \
	> /tmp/irq_test_out.txt 2>&1 || true
cat /tmp/irq_test_out.txt

echo "== 4. 检查结果 =="
# 期望: X=0x00000001 (ISR执行一次), B=0xCAFEBABE (IRET正确返回), RIN2=0 (挂起清除)
if grep -q "X =0x00000001" /tmp/irq_test_out.txt \
   && grep -q "B =0xCAFEBABE" /tmp/irq_test_out.txt \
   && grep -q "RIN2=0x00000000" /tmp/irq_test_out.txt; then
	echo "PASS: X=1, B=0xCAFEBABE, RIN2=0"
else
	echo "FAIL: 请检查上面的寄存器转储"
	exit 1
fi
