// pit.c - PIT设备的模拟

#include "pit.h"
#include "../interrupt.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

static inline uint64_t time2ns(struct timespec now) {
	return (uint64_t)now.tv_sec*1000000000ULL+(uint64_t)now.tv_nsec;
}

static inline bool pit_get_te(uint8_t ctrl) {
	return ((ctrl&0x80)>>7);
}

static inline uint8_t pit_get_tu(uint8_t ctrl) {
	return ((ctrl&0x18)>>3);
}

static inline uint8_t pit_get_ie(uint8_t ctrl) {
	return ((ctrl&0x40)>>6);
}

static inline uint8_t pit_get_se(uint8_t ctrl) {
	return ((ctrl&0x20)>>5);
}

void pit_init(Device *dev) {
	Dev_PIT *priv=calloc(1, sizeof(Dev_PIT));
	if(!priv) {
		perror("Failed to calloc for PIT");
		exit(EXIT_FAILURE);
	}
	dev->type=DEVICE_TYPE_PIT;
	dev->name="PIT";
	dev->base_port=PIT_BASE_PORT;
	dev->port_cnt=2;
	dev->is_mmio=false;
	dev->irq=PIT_IRQ;
	dev->ops=&pit_ops;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	priv->last_time_ns=time2ns(now);
	priv->accu_ns=0;
	priv->ctrl=0x00;
	priv->rel=0;
	priv->counter=0;
	priv->will_reload=0;
	priv->triggered=0;
	priv->counter_loaded=0;
	dev->private_data=priv;
	return;
}

void pit_tick(Device *dev, uint64_t d_tick) {
	if(!(dev->private_data)) return;
	// 检查TE
	uint8_t ctrl=((Dev_PIT *)(dev->private_data))->ctrl;
	if(!pit_get_te(ctrl)) return;
	// 处理will_reload
	if(((Dev_PIT *)(dev->private_data))->will_reload) {
		((Dev_PIT *)(dev->private_data))->counter=((Dev_PIT *)(dev->private_data))->rel;
		((Dev_PIT *)(dev->private_data))->will_reload=false;
		((Dev_PIT *)(dev->private_data))->counter_loaded=true;
	}
	// 根据TU选择更新方式
	int64_t units=0;
	switch(pit_get_tu(ctrl)) {
		case 0:						// 微秒模式
		case 1:						// 毫秒模式
		case 2:						// 秒模式
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			uint64_t now_ns=time2ns(now);
			((Dev_PIT *)(dev->private_data))->accu_ns+=now_ns-((Dev_PIT *)(dev->private_data))->last_time_ns;
			((Dev_PIT *)(dev->private_data))->last_time_ns=now_ns;	// 更新基准时间（否则累计的是总耗时）
			uint32_t num=0;
			if(pit_get_tu(ctrl)==0) num=1000;
			if(pit_get_tu(ctrl)==1) num=1000000;
			if(pit_get_tu(ctrl)==2) num=1000000000;
			units=(int64_t)(((Dev_PIT *)(dev->private_data))->accu_ns / num);
			((Dev_PIT *)(dev->private_data))->accu_ns -= units*num;			// 保留余数
			break;
		case 3:						// 周期模式，直接减去传入的\Delta CPU周期
			((Dev_PIT *)(dev->private_data))->counter-=d_tick;
			break;
		default:
			break;
	}
	if(units>0) {
		uint32_t cnt=((Dev_PIT *)(dev->private_data))->counter;
		if(cnt!=0) {
			if((uint64_t)units>=cnt) ((Dev_PIT *)(dev->private_data))->counter=0;	// 防止下溢
			else ((Dev_PIT *)(dev->private_data))->counter=cnt-(uint32_t)units;
		}
	}
	if(((Dev_PIT *)(dev->private_data))->counter==0) {
		if(pit_get_ie(ctrl)) {
			if(!((Dev_PIT *)(dev->private_data))->triggered
			   && ((Dev_PIT *)(dev->private_data))->counter_loaded) {	// 计数器未写入过时不触发
				irq_set(dev->cpu, PIT_IRQ);
			}
		}
		if(pit_get_se(ctrl)) {
			// 单次触发：保持0，等待重新写入计数器后再继续计时
			((Dev_PIT *)(dev->private_data))->counter=0;
			((Dev_PIT *)(dev->private_data))->triggered=true;
		} else {
			// 周期模式：复位计数器，继续计时，允许下次再次触发
			((Dev_PIT *)(dev->private_data))->counter=((Dev_PIT *)(dev->private_data))->rel;
			((Dev_PIT *)(dev->private_data))->triggered=false;
		}
	}
}

uint32_t pit_read_port(Device *dev, uint16_t port, uint8_t size) {
	if(!dev->private_data) return 0xffffffff;
	size=size;
	if(port==PIT_BASE_PORT) return (uint32_t)(((Dev_PIT *)(dev->private_data))->ctrl);
	else return (uint32_t)(((Dev_PIT *)(dev->private_data))->counter);
}

void pit_write_port(Device *dev, uint16_t port, uint32_t data, uint8_t size) {
	if(!dev->private_data) return;
	size=size;
	if(port==PIT_BASE_PORT) {
		((Dev_PIT *)(dev->private_data))->ctrl=(uint8_t)(data&0xff);
		((Dev_PIT *)(dev->private_data))->triggered=false;
	} else {
		((Dev_PIT *)(dev->private_data))->rel=data;
		((Dev_PIT *)(dev->private_data))->will_reload=true;	// 仅在写入计数器时调度重载
		((Dev_PIT *)(dev->private_data))->triggered=false;	// 重新武装，允许再次触发
		((Dev_PIT *)(dev->private_data))->counter_loaded=true;
	}
	return;
}

void pit_reset(Device *dev) {
	if(!dev->private_data) return;
	((Dev_PIT *)(dev->private_data))->ctrl=0x00;
	((Dev_PIT *)(dev->private_data))->rel=0;
	((Dev_PIT *)(dev->private_data))->counter=0;
	((Dev_PIT *)(dev->private_data))->accu_ns=0;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	((Dev_PIT *)(dev->private_data))->last_time_ns=time2ns(now);
	((Dev_PIT *)(dev->private_data))->triggered=0;
	((Dev_PIT *)(dev->private_data))->will_reload=0;
	((Dev_PIT *)(dev->private_data))->counter_loaded=0;
	return;
}

void pit_dump(Device *dev) {
	fprintf(stderr, "INFO: device name=%s\n", dev->name);
}

void pit_destroy(Device *dev) {
	if(dev->private_data) free(dev->private_data);
	dev->private_data=NULL;
	return;
}

