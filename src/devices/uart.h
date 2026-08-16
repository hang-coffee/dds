// uart.h - UART 设备（见 dev_specification.md 第 3 节）
// 端口:
//   0x14 UART_RXD  8位  输入寄存器（读 FIFO 一个字节）
//   0x15 UART_RXST 16位 输入状态（读=状态, 写=W1C）
//   0x16 UART_TXD  8位  输出寄存器（写即发送）
//   0x17 UART_TXST 16位 输出状态（读=状态, 写=W1C）
//   0x18 UART_CFG  32位 配置寄存器

#ifndef UART_H
#define UART_H

#include "../device.h"

struct Dev_UART_t;
typedef struct Dev_UART_t Dev_UART;

struct Dev_UART_t {
	uint32_t config;			// 0x18
	// 接收 FIFO
	uint8_t rx_buf[16];
	uint8_t rx_head;			// 队首（出）
	uint8_t rx_tail;			// 队尾（入）
	uint8_t rx_count;
	bool rx_overflow;
	// 发送
	bool tx_busy;
	uint32_t tx_delay_left;		// 剩余 tick
	bool tx_overflow;
};

void uart_init(Device *dev);
void uart_tick(Device *dev, uint64_t d_tick);
uint32_t uart_read_port(Device *dev, uint16_t port, uint8_t size);
void uart_write_port(Device *dev, uint16_t port, uint32_t data, uint8_t size);
void uart_reset(Device *dev);
void uart_dump(Device *dev);
void uart_destroy(Device *dev);

// 宿主注入一个接收字节（外部输入；等效串口收到数据）
void uart_inject_byte(DOCTOR_CPU *cpu, uint8_t byte);

static const Device_ops uart_ops={
	.init=uart_init,
	.tick=uart_tick,
	.read_port=uart_read_port,
	.write_port=uart_write_port,
	.reset=uart_reset,
	.dump=uart_dump,
	.destroy=uart_destroy,
};

#define UART_BASE_PORT 0x14
#define UART_PORT_RXD  0x14
#define UART_PORT_RXST 0x15
#define UART_PORT_TXD  0x16
#define UART_PORT_TXST 0x17
#define UART_PORT_CFG  0x18

#define UART_CFG_EN    0x00000001
#define UART_CFG_IE    0x00000002
#define UART_CFG_LOOP  0x00000004
#define UART_CFG_TXDELAY_MASK 0x000000f8
#define UART_CFG_TXDELAY_SHIFT 3

#define UART_ST_RX_READY    0x0001
#define UART_ST_RX_OVERFLOW 0x0002
#define UART_ST_TX_READY    0x0001
#define UART_ST_TX_OVERFLOW 0x0002

#define UART_FIFO_SIZE 16
#define UART_IRQ 1

#endif
