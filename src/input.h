// input.h - 宿主键盘输入 → KBC 设备
// 模拟器执行期间把终端按键转为 Set 1 扫描码注入 KBC 设备。
// 安全键 Ctrl+C: 第一次按 → 暂停模拟器（键盘输入不再转发）；再次按 → 解除暂停。
// 暂停时按 q → 退出模拟器。
// 仅在 stdin 为终端时启用（重定向/管道/测试下不生效）。

#ifndef INPUT_H
#define INPUT_H

#include "cpu.h"

// 暂停标志（cpu_run 检查；input.c 维护）
extern volatile bool sim_paused;

void input_init(void);			// 进入 raw 终端模式（非终端时无操作）
void input_poll(DOCTOR_CPU *cpu);	// 非阻塞读取并处理一个输入批次

#endif
