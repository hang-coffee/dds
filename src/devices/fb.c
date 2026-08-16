// fb.c - FB 帧缓冲设备
// 帧缓冲通过 Display 层渲染：模拟器/程序只写显存与调色板、触发刷新，
// 具体显示方式（终端/图像文件/内存）由选定的 Display 后端决定。

#include "fb.h"
#include "../display.h"
#include "../cpu.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void fb_init(Device *dev) {
	Dev_FB *priv=calloc(1, sizeof(Dev_FB));
	if(!priv) {
		perror("Failed to calloc for FB");
		exit(EXIT_FAILURE);
	}
	dev->type=DEVICE_TYPE_FB;
	dev->name="FB";
	dev->base_port=FB_BASE_PORT;
	dev->port_cnt=6;
	dev->is_mmio=false;
	dev->irq=0;
	dev->ops=&fb_ops;
	dev->private_data=priv;
	return;
}

void fb_tick(Device *dev, uint64_t d_tick) {
	(void)dev; (void)d_tick;
	return;		// 刷新由端口命令触发
}

uint32_t fb_read_port(Device *dev, uint16_t port, uint8_t size) {
	(void)size;
	if(!dev->private_data) return 0xffffffff;
	Dev_FB *fb=(Dev_FB *)dev->private_data;
	switch(port-FB_BASE_PORT) {
		case 0: return fb->ctrl;
		case 1: return fb->fb_base;
		case 2: return fb->width;
		case 3: return fb->height;
		case 5: return fb->pal_base;
		default: return 0;
	}
}

// 从数据区读取显存+调色板，填充 Display 帧缓冲并刷新
static void fb_refresh(Device *dev) {
	Dev_FB *fb=(Dev_FB *)dev->private_data;
	if(!(fb->ctrl & FB_CTRL_EN)) return;			// 未使能
	Display *d=display_get_global();
	if(!d || !d->fb) return;
	if(fb->fb_base==0 || fb->pal_base==0) return;
	// 尺寸以 Display 帧缓冲为准（--display-size 决定）
	uint32_t w=d->width, h=d->height;
	if(fb->width && fb->height && (fb->width!=w || fb->height!=h)) {
		fprintf(stderr, "WARNING: FB 端口尺寸 %ux%u 与 Display %ux%u 不一致，以 Display 为准\n",
			fb->width, fb->height, w, h);
	}
	// 边界检查：显存/调色板须落在数据空间内
	DOCTOR_CPU *cpu=dev->cpu;
	if(cpu) {
		uint64_t fb_end=(uint64_t)fb->fb_base+(uint64_t)w*h;
		uint64_t pal_end=(uint64_t)fb->pal_base+(uint64_t)256*3;
		if(fb_end > DATA_SIZE || pal_end > DATA_SIZE) {
			fprintf(stderr, "WARNING: FB 显存/调色板越界，刷新忽略\n");
			return;
		}
		display_blit_indexed(d, &cpu->data_mem[fb->fb_base],
		                     &cpu->data_mem[fb->pal_base], 256);
		display_flush(d);
	}
}

void fb_write_port(Device *dev, uint16_t port, uint32_t data, uint8_t size) {
	(void)size;
	if(!dev->private_data) return;
	Dev_FB *fb=(Dev_FB *)dev->private_data;
	switch(port-FB_BASE_PORT) {
		case 0: fb->ctrl=(uint8_t)(data&0xff); break;
		case 1: fb->fb_base=data; break;
		case 2: fb->width=(uint16_t)(data&0xffff); break;
		case 3: fb->height=(uint16_t)(data&0xffff); break;
		case 4: fb_refresh(dev); break;			// 刷新命令
		case 5: fb->pal_base=data; break;
		default: break;
	}
	return;
}

void fb_reset(Device *dev) {
	if(!dev->private_data) return;
	Dev_FB *fb=(Dev_FB *)dev->private_data;
	fb->ctrl=0;
	fb->fb_base=0;
	fb->width=0;
	fb->height=0;
	fb->pal_base=0;
	return;
}

void fb_dump(Device *dev) {
	if(!dev->private_data) return;
	Dev_FB *fb=(Dev_FB *)dev->private_data;
	fprintf(stderr, "INFO: FB: ctrl=%02X base=%08X %ux%u pal=%08X\n",
		fb->ctrl, fb->fb_base, fb->width, fb->height, fb->pal_base);
}

void fb_destroy(Device *dev) {
	if(dev->private_data) free(dev->private_data);
	dev->private_data=NULL;
	return;
}
