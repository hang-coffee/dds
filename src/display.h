// display.h - Display 层：统一显示后端抽象
//
// 模拟器与设备只需发出"显示请求"（字符输出 / 帧缓冲像素），
// 由本层按选定的后端（TTY / ANSI / PPM / 内存 FB）决定实际显示方式，
// 模拟器本身不关心渲染细节。
//
// 后端选择（doctor_sim 命令行）:
//   --display <tty|ansi|ppm|fb>   默认 tty
//   --display-file <path>          PPM 后端输出文件（默认 display.ppm）
//   --display-size <WxH>           帧缓冲尺寸（默认 320x200）

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

// ---- 后端类型 ----
typedef enum {
	DISPLAY_BACKEND_TTY=0,		// 纯文本：字符直接写 stdout（默认，无 GUI 依赖）
	DISPLAY_BACKEND_ANSI,		// ANSI：字符带颜色/属性，帧缓冲刷新输出字符画
	DISPLAY_BACKEND_PPM,		// PPM：帧缓冲导出为 P6 PPM 图像文件
	DISPLAY_BACKEND_FB,			// FB：帧缓冲保留在内存中（display_get_pixel 可读回）
	DISPLAY_BACKEND_COUNT
} Display_backend;

// ---- RGB 像素 ----
typedef struct {
	uint8_t r, g, b;
} Display_pixel;

// ---- 字符属性（ANSI 后端使用；TTY/FB 忽略）----
// 低 4 位：VGA 前景色；bit4：加粗
#define DISPLAY_FG_BLACK    0x00
#define DISPLAY_FG_RED      0x01
#define DISPLAY_FG_GREEN    0x02
#define DISPLAY_FG_YELLOW   0x03
#define DISPLAY_FG_BLUE     0x04
#define DISPLAY_FG_MAGENTA  0x05
#define DISPLAY_FG_CYAN     0x06
#define DISPLAY_FG_WHITE    0x07
#define DISPLAY_FG_BRIGHT   0x08	// 与 0-7 组合为亮色（8-15）
#define DISPLAY_ATTR_BOLD   0x10

// ---- Display 实例 ----
typedef struct Display_t {
	Display_backend backend;
	const char *name;			// 后端名称
	uint32_t width, height;		// 帧缓冲尺寸
	Display_pixel *fb;			// RGB 帧缓冲（width*height）
	void *priv;					// 后端私有数据
} Display;

// ---- 生命周期 ----
void display_init(Display *d, Display_backend backend, uint32_t width, uint32_t height);
void display_destroy(Display *d);

// ---- 字符输出（TTY/ANSI）----
void display_putc(Display *d, char c, uint8_t attr);
void display_puts(Display *d, const char *s, uint8_t attr);

// ---- 帧缓冲 ----
void display_clear(Display *d, Display_pixel color);
void display_set_pixel(Display *d, uint32_t x, uint32_t y, Display_pixel c);
// 从索引色显存填充帧缓冲：indexed 为 width*height 字节，palette_rgb 每项 3 字节 RGB
void display_blit_indexed(Display *d, const uint8_t *indexed,
                          const uint8_t *palette_rgb, uint32_t palette_entries);
// 刷新：把当前帧缓冲交给后端渲染（ANSI 字符画 / PPM 写文件 / FB 无操作）
void display_flush(Display *d);

// ---- 查询（FB 后端）----
Display_pixel display_get_pixel(Display *d, uint32_t x, uint32_t y);

// ---- 后端选择辅助 ----
Display_backend display_backend_from_name(const char *name);	// 未知名称返回 DISPLAY_BACKEND_TTY
const char *display_backend_name(Display_backend b);
// 设置 PPM 后端输出文件路径（在 display_init 之前调用）
void display_set_ppm_path(const char *path);

// ---- 全局实例（供设备/模拟器使用）----
Display *display_get_global(void);

#endif
