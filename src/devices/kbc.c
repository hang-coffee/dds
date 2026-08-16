// kbc.c - AT 兼容键盘控制器（8042/PS/2 风格）
// 读 0x1A 取输出缓冲扫描码；写 0x1B 发 8042 命令；写 0x1A 发键盘命令。
// OBF 就绪（输出 FIFO 非空）且命令字节 bit0=1 且未禁用 → 每个 tick 请求 IRQ1
// （电平式：ISR 必须读 0x1A 清空 FIFO 以解除中断）。

#include "kbc.h"
#include "../interrupt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static bool kbc_out_push(Dev_KBC *k, uint8_t byte) {
	if(k->out_count >= 16) return false;		// FIFO 满（宿主输入突发时缓冲）
	k->out_buf[k->out_tail]=byte;
	k->out_tail=(k->out_tail+1)%16;
	k->out_count++;
	return true;
}

static uint8_t kbc_out_pop(Dev_KBC *k) {
	if(k->out_count==0) return 0x00;
	uint8_t b=k->out_buf[k->out_head];
	k->out_head=(k->out_head+1)%16;
	k->out_count--;
	return b;
}

// 键盘命令响应（写 0x1A）
static void kbc_keyboard_command(Dev_KBC *k, uint8_t cmd) {
	switch(cmd) {
		case 0xEE:						// 回显
			kbc_out_push(k, 0xEE);
			break;
		case 0xED:						// 设置 LED
		case 0xF4:						// 启用
		case 0xF5:						// 禁用
			kbc_out_push(k, 0xFA);		// ACK
			break;
		case 0xF0:						// 选择扫描码集 → ACK + 集号(Set 1)
			kbc_out_push(k, 0xFA);
			kbc_out_push(k, 0x01);
			break;
		case 0xFF:						// 复位 → ACK + 自检通过
			kbc_out_push(k, 0xFA);
			kbc_out_push(k, 0xAA);
			break;
		default:
			kbc_out_push(k, 0xFA);		// 未知命令回 ACK
			break;
	}
}

void kbc_init(Device *dev) {
	Dev_KBC *priv=calloc(1, sizeof(Dev_KBC));
	if(!priv) {
		perror("Failed to calloc for KBC");
		exit(EXIT_FAILURE);
	}
	dev->type=DEVICE_TYPE_KBC;
	dev->name="KBC";
	dev->base_port=KBC_BASE_PORT;
	dev->port_cnt=2;
	dev->is_mmio=false;
	dev->irq=KBC_IRQ;
	dev->ops=&kbc_ops;
	dev->private_data=priv;
	return;
}

void kbc_tick(Device *dev, uint64_t d_tick) {
	(void)d_tick;
	if(!dev->private_data || !dev->cpu) return;
	Dev_KBC *k=(Dev_KBC *)dev->private_data;
	// 电平式中断：输出缓冲就绪且中断使能且键盘未禁用 → 持续请求 IRQ1
	if(k->out_count>0 && (k->cmd_byte & KBC_CMD_IRQ) && !(k->cmd_byte & KBC_CMD_DISABLE)) {
		irq_set(dev->cpu, KBC_IRQ);
	}
	return;
}

uint32_t kbc_read_port(Device *dev, uint16_t port, uint8_t size) {
	(void)size;
	if(!dev->private_data) return 0;
	Dev_KBC *k=(Dev_KBC *)dev->private_data;
	switch(port) {
		case KBC_PORT_DATA:
			return kbc_out_pop(k);
		case KBC_PORT_STAT: {
			uint8_t st=0;
			if(k->out_count>0) st|=KBC_ST_OBF;
			if(k->sys_flag)    st|=KBC_ST_SYS;
			if(k->cmd_data_flag) st|=KBC_ST_CD;
			if(!(k->cmd_byte & KBC_CMD_DISABLE)) st|=KBC_ST_KBD_INH;
			return st;
		}
		default:
			return 0;
	}
}

void kbc_write_port(Device *dev, uint16_t port, uint32_t data, uint8_t size) {
	(void)size;
	if(!dev->private_data) return;
	Dev_KBC *k=(Dev_KBC *)dev->private_data;
	uint8_t byte=(uint8_t)(data&0xff);
	switch(port) {
		case KBC_PORT_DATA:
			if(k->wait_cmd_byte) {
				// 写命令字节数据
				k->cmd_byte=byte;
				k->wait_cmd_byte=false;
			} else {
				kbc_keyboard_command(k, byte);
			}
			k->cmd_data_flag=false;		// 数据写入
			break;
		case KBC_PORT_STAT:
			// 8042 命令
			switch(byte) {
				case 0xAA:				// 自测
					kbc_out_push(k, 0x55);
					k->sys_flag=true;
					break;
				case 0xAB:				// 接口测试
					kbc_out_push(k, 0x00);
					break;
				case 0xAD:				// 禁用键盘
					k->cmd_byte |= KBC_CMD_DISABLE;
					break;
				case 0xAE:				// 启用键盘
					k->cmd_byte &= ~KBC_CMD_DISABLE;
					break;
				case 0x20:				// 读命令字节
					kbc_out_push(k, k->cmd_byte);
					break;
				case 0x60:				// 写命令字节（等待后续数据）
					k->wait_cmd_byte=true;
					break;
				case 0xFE:				// 复位
					kbc_out_push(k, 0xFA);
					break;
				default:
					break;
			}
			k->cmd_data_flag=true;		// 命令写入
			break;
		default:
			break;
	}
	return;
}

bool kbc_inject_scancode(DOCTOR_CPU *cpu, uint8_t byte) {
	Device *dev=device_find(cpu, KBC_PORT_DATA);
	if(!dev || !dev->private_data) return false;
	Dev_KBC *k=(Dev_KBC *)dev->private_data;
	if(k->out_count >= 16) return false;		// 缓冲满
	return kbc_out_push(k, byte);
}

void kbc_reset(Device *dev) {
	if(!dev->private_data) return;
	Dev_KBC *k=(Dev_KBC *)dev->private_data;
	memset(k, 0, sizeof(*k));
	return;
}

void kbc_dump(Device *dev) {
	if(!dev->private_data) return;
	Dev_KBC *k=(Dev_KBC *)dev->private_data;
	fprintf(stderr, "INFO: KBC: cmd=%02X out_count=%u sys=%d\n",
		k->cmd_byte, k->out_count, k->sys_flag);
}

void kbc_destroy(Device *dev) {
	if(dev->private_data) free(dev->private_data);
	dev->private_data=NULL;
	return;
}
