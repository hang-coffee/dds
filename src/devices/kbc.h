// kbc.h - AT 兼容键盘控制器（见 dev_specification.md 第 4 节）
// 端口: 0x1A ↔ PC 0x60（数据）, 0x1B ↔ PC 0x64（状态/命令）
// 键盘默认 Set 1；输出缓冲为 2 字节 FIFO；OBF 就绪且命令字节 bit0=1 时请求 IRQ1。

#ifndef KBC_H
#define KBC_H

#include "../device.h"

struct Dev_KBC_t;
typedef struct Dev_KBC_t Dev_KBC;

struct Dev_KBC_t {
	uint8_t cmd_byte;			// 8042 命令字节
	bool sys_flag;				// 自测通过（状态 bit2）
	bool cmd_data_flag;			// 最近写入 0x1B(命令)=1 / 0x1A(数据)=0（状态 bit3）
	bool wait_cmd_byte;			// 等待写命令字节数据
	// 输出缓冲 FIFO（扫描码/控制器响应）
	uint8_t out_buf[16];
	uint8_t out_head;
	uint8_t out_tail;
	uint8_t out_count;
};

void kbc_init(Device *dev);
void kbc_tick(Device *dev, uint64_t d_tick);
uint32_t kbc_read_port(Device *dev, uint16_t port, uint8_t size);
void kbc_write_port(Device *dev, uint16_t port, uint32_t data, uint8_t size);
void kbc_reset(Device *dev);
void kbc_dump(Device *dev);
void kbc_destroy(Device *dev);

// 宿主注入一个 Set 1 扫描码字节（等效键盘按键事件）。
// 输出缓冲满时返回 false（可稍后重试）。
bool kbc_inject_scancode(DOCTOR_CPU *cpu, uint8_t byte);

static const Device_ops kbc_ops={
	.init=kbc_init,
	.tick=kbc_tick,
	.read_port=kbc_read_port,
	.write_port=kbc_write_port,
	.reset=kbc_reset,
	.dump=kbc_dump,
	.destroy=kbc_destroy,
};

#define KBC_BASE_PORT 0x1A
#define KBC_PORT_DATA 0x1A
#define KBC_PORT_STAT 0x1B

// 状态寄存器位
#define KBC_ST_OBF      0x01
#define KBC_ST_IBF      0x02
#define KBC_ST_SYS      0x04
#define KBC_ST_CD       0x08
#define KBC_ST_KBD_INH  0x10

// 命令字节位
#define KBC_CMD_IRQ     0x01
#define KBC_CMD_DISABLE 0x08

#define KBC_IRQ 1		// PC 标准：键盘 IRQ1

#endif
