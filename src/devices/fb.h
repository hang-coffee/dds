// fb.h - FB 帧缓冲设备（通过 Display 层渲染）
// 端口布局:
//   0x20 控制寄存器 (bit0=EN 使能, 未使能时刷新忽略)
//   0x21 显存基址 (DWORD, 数据区地址, 索引色 width*height 字节)
//   0x22 宽度 (WORD)
//   0x23 高度 (WORD)
//   0x24 刷新命令 (写入任意值 → 读取显存+调色板 → display_blit_indexed + flush)
//   0x25 调色板基址 (DWORD, 数据区地址, 每项 3 字节 RGB, 最多 256 项)

#ifndef FB_H
#define FB_H

#include "../device.h"

struct Dev_FB_t;
typedef struct Dev_FB_t Dev_FB;

struct Dev_FB_t {
	uint8_t ctrl;				// 控制寄存器 0x20
	uint32_t fb_base;			// 显存基址 0x21
	uint16_t width;				// 宽度 0x22
	uint16_t height;			// 高度 0x23
	uint32_t pal_base;			// 调色板基址 0x25
};

void fb_init(Device *dev);
void fb_tick(Device *dev, uint64_t d_tick);
uint32_t fb_read_port(Device *dev, uint16_t port, uint8_t size);
void fb_write_port(Device *dev, uint16_t port, uint32_t data, uint8_t size);
void fb_reset(Device *dev);
void fb_dump(Device *dev);
void fb_destroy(Device *dev);

static const Device_ops fb_ops={
	.init=fb_init,
	.tick=fb_tick,
	.read_port=fb_read_port,
	.write_port=fb_write_port,
	.reset=fb_reset,
	.dump=fb_dump,
	.destroy=fb_destroy,
};

#define FB_BASE_PORT 0x20
#define FB_CTRL_EN   0x01

#endif
