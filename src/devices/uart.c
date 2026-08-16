// uart.c - UART 设备
// 接收 FIFO（16 字节，来源：环回 / uart_inject_byte）；
// 发送单字节缓冲，输出交给 Display 层，环回时同时进入接收 FIFO。
// 接收就绪（FIFO 空→非空）且 EN|IE 时请求 IRQ1。

#include "uart.h"
#include "../interrupt.h"
#include "../display.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static bool uart_enabled(Dev_UART *u) {
	return (u->config & UART_CFG_EN)!=0;
}

static uint32_t uart_tx_delay(Dev_UART *u) {
	return (u->config & UART_CFG_TXDELAY_MASK) >> UART_CFG_TXDELAY_SHIFT;
}

void uart_init(Device *dev) {
	Dev_UART *priv=calloc(1, sizeof(Dev_UART));
	if(!priv) {
		perror("Failed to calloc for UART");
		exit(EXIT_FAILURE);
	}
	dev->type=DEVICE_TYPE_UART;
	dev->name="UART";
	dev->base_port=UART_BASE_PORT;
	dev->port_cnt=5;
	dev->is_mmio=false;
	dev->irq=UART_IRQ;
	dev->ops=&uart_ops;
	dev->private_data=priv;
	return;
}

// 接收 FIFO 入队；满则丢弃并置溢出位
static void uart_rx_push(Dev_UART *u, uint8_t byte) {
	if(u->rx_count >= UART_FIFO_SIZE) {
		u->rx_overflow=true;
		return;
	}
	u->rx_buf[u->rx_tail]=byte;
	u->rx_tail=(u->rx_tail+1)%UART_FIFO_SIZE;
	u->rx_count++;
}

// 接收 FIFO 出队；空返回 0xFF
static uint8_t uart_rx_pop(Dev_UART *u) {
	if(u->rx_count==0) return 0xff;
	uint8_t b=u->rx_buf[u->rx_head];
	u->rx_head=(u->rx_head+1)%UART_FIFO_SIZE;
	u->rx_count--;
	return b;
}

// 发送一个字节：输出到 Display 层；环回时进入接收 FIFO
static void uart_transmit(Device *dev, Dev_UART *u, uint8_t byte) {
	display_putc(display_get_global(), (char)byte, DISPLAY_FG_WHITE);
	if(u->config & UART_CFG_LOOP) {
		uart_rx_push(u, byte);
		// 接收就绪且 IE → 请求 IRQ1（环回路径）
		if(u->config & UART_CFG_IE && dev->cpu) {
			irq_set(dev->cpu, UART_IRQ);
		}
	}
	uint32_t delay=uart_tx_delay(u);
	if(delay>0) {
		u->tx_busy=true;
		u->tx_delay_left=delay;
	}
}

void uart_inject_byte(DOCTOR_CPU *cpu, uint8_t byte) {
	// 找到 UART 设备实例
	Device *dev=device_find(cpu, UART_PORT_RXD);
	if(!dev || !dev->private_data) return;
	Dev_UART *u=(Dev_UART *)dev->private_data;
	if(!uart_enabled(u)) return;
	uart_rx_push(u, byte);
	// FIFO 空→非空且 IE 时请求中断
	if(u->config & UART_CFG_IE) {
		irq_set(cpu, UART_IRQ);
	}
}

void uart_tick(Device *dev, uint64_t d_tick) {
	(void)d_tick;
	if(!dev->private_data) return;
	Dev_UART *u=(Dev_UART *)dev->private_data;
	if(!uart_enabled(u)) return;
	// 发送进行中：消耗 tick
	if(u->tx_busy) {
		if(u->tx_delay_left>0) u->tx_delay_left--;
		if(u->tx_delay_left==0) u->tx_busy=false;
	}
	return;
}

uint32_t uart_read_port(Device *dev, uint16_t port, uint8_t size) {
	(void)size;
	if(!dev->private_data) return 0xffffffff;
	Dev_UART *u=(Dev_UART *)dev->private_data;
	switch(port) {
		case UART_PORT_RXD:
			return uart_enabled(u) ? uart_rx_pop(u) : 0xff;
		case UART_PORT_RXST: {
			uint16_t st=0;
			if(uart_enabled(u)) {
				if(u->rx_count>0) st|=UART_ST_RX_READY;
				if(u->rx_overflow) st|=UART_ST_RX_OVERFLOW;
			}
			return st;
		}
		case UART_PORT_TXST: {
			uint16_t st=0;
			if(uart_enabled(u)) {
				if(!u->tx_busy) st|=UART_ST_TX_READY;
				if(u->tx_overflow) st|=UART_ST_TX_OVERFLOW;
			}
			return st;
		}
		case UART_PORT_CFG:
			return u->config;
		default:
			return 0;
	}
}

void uart_write_port(Device *dev, uint16_t port, uint32_t data, uint8_t size) {
	(void)size;
	if(!dev->private_data) return;
	Dev_UART *u=(Dev_UART *)dev->private_data;
	switch(port) {
		case UART_PORT_RXD:
			break;							// 只读
		case UART_PORT_RXST:				// W1C 清除溢出位
			if(data & UART_ST_RX_OVERFLOW) u->rx_overflow=false;
			break;
		case UART_PORT_TXD:
			if(!uart_enabled(u)) break;
			if(!u->tx_busy) {
				uart_transmit(dev, u, (uint8_t)(data&0xff));
			} else {
				u->tx_overflow=true;		// 忙时写入被丢弃
			}
			break;
		case UART_PORT_TXST:				// W1C 清除溢出位
			if(data & UART_ST_TX_OVERFLOW) u->tx_overflow=false;
			break;
		case UART_PORT_CFG:
			u->config=data;
			// 关闭使能时复位发送状态
			if(!uart_enabled(u)) {
				u->tx_busy=false;
				u->tx_delay_left=0;
			}
			break;
		default:
			break;
	}
	return;
}

void uart_reset(Device *dev) {
	if(!dev->private_data) return;
	Dev_UART *u=(Dev_UART *)dev->private_data;
	memset(u, 0, sizeof(*u));
	return;
}

void uart_dump(Device *dev) {
	if(!dev->private_data) return;
	Dev_UART *u=(Dev_UART *)dev->private_data;
	fprintf(stderr, "INFO: UART: cfg=%08X rx_count=%u tx_busy=%d\n",
		u->config, u->rx_count, u->tx_busy);
}

void uart_destroy(Device *dev) {
	if(dev->private_data) free(dev->private_data);
	dev->private_data=NULL;
	return;
}
