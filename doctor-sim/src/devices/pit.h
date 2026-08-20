// pit.h - PIT设备的模拟

#ifndef PIT_H
#define PIT_H

#include "../device.h"
#include <time.h>

struct Dev_PIT_t;
typedef struct Dev_PIT_t Dev_PIT;

struct Dev_PIT_t {						// PIT的私有数据
	uint8_t ctrl;						// 控制寄存器 0x10
	uint32_t rel;						// 计数器 0x11
	uint32_t counter;					// 计数器
	
	uint64_t last_time_ns;				// 精确到纳秒的 上一次更新的真实时间
	uint64_t accu_ns;					// 真实时间余数

	bool will_reload;					// 是否要重载？
	bool triggered;						// 是否触发了中断？
	bool counter_loaded;				// 计数器是否已被写入/重载过（防止TE使能时counter=0误触发）
};

void pit_init(Device *dev);
void pit_tick(Device *dev, uint64_t d_tick);
uint32_t pit_read_port(Device *dev, uint16_t port, uint8_t size);
void pit_write_port(Device *dev, uint16_t port, uint32_t data, uint8_t size);
void pit_reset(Device *dev);
void pit_dump(Device *dev);
void pit_destroy(Device *dev);

static const Device_ops pit_ops={
	.init=pit_init,
	.tick=pit_tick,
	.read_port=pit_read_port,
	.write_port=pit_write_port,
	.reset=pit_reset,
	.dump=pit_dump,
	.destroy=pit_destroy,
};

#define PIT_BASE_PORT 0x10
#define PIT_IRQ 0x00

#endif

