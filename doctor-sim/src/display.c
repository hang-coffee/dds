// display.c - Display 层：统一显示后端实现
//
// 后端:
//   TTY   字符 → stdout（默认）；帧缓冲刷新忽略（面向字符流）
//   ANSI  字符带 ANSI 颜色/加粗；帧缓冲刷新输出亮度字符画（小尺寸）
//   PPM   帧缓冲 → P6 PPM 文件（每次 flush 覆盖重写）
//   FB    帧缓冲保留在内存，display_get_pixel 可读回（程序化消费）

#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Display g_display;		// 全局 Display 实例
static char g_ppm_path[512]="display.ppm";	// PPM 后端输出文件

Display *display_get_global(void) {
	return &g_display;
}

void display_set_ppm_path(const char *path) {
	if(path) snprintf(g_ppm_path, sizeof(g_ppm_path), "%s", path);
}

const char *display_backend_name(Display_backend b) {
	switch(b) {
		case DISPLAY_BACKEND_TTY:  return "tty";
		case DISPLAY_BACKEND_ANSI: return "ansi";
		case DISPLAY_BACKEND_PPM:  return "ppm";
		case DISPLAY_BACKEND_FB:   return "fb";
		default:                   return "tty";
	}
}

Display_backend display_backend_from_name(const char *name) {
	if(!name) return DISPLAY_BACKEND_TTY;
	if(strcmp(name, "ansi")==0)  return DISPLAY_BACKEND_ANSI;
	if(strcmp(name, "ppm")==0)   return DISPLAY_BACKEND_PPM;
	if(strcmp(name, "fb")==0)    return DISPLAY_BACKEND_FB;
	return DISPLAY_BACKEND_TTY;
}

// ==================== 后端私有数据 ====================

// PPM 后端: 输出文件路径 + 句柄
typedef struct {
	char path[512];
	FILE *fp;
} Ppm_priv;

// ==================== TTY 后端 ====================

static void tty_putc(Display *d, char c, uint8_t attr) {
	(void)d; (void)attr;
	fputc(c, stdout);
	fflush(stdout);		// 实时输出（UART 等串口语义）
}

static void tty_puts(Display *d, const char *s, uint8_t attr) {
	(void)d; (void)attr;
	fputs(s, stdout);
	fflush(stdout);
}

static void tty_flush(Display *d) {
	(void)d;		// 字符流模式：帧缓冲刷新无操作
}

// ==================== ANSI 后端 ====================

// 输出 ANSI 前景色转义（16 色 + 加粗）
static void ansi_set_color(uint8_t attr) {
	uint8_t fg = attr & 0x0f;
	bool bold = (attr & DISPLAY_ATTR_BOLD)!=0;
	if(bold) fputs("\033[1m", stdout);
	if(fg < 8) {
		printf("\033[3%um", fg);
	} else {
		printf("\033[9%um", fg-8);
	}
}

static void ansi_reset_color(void) {
	fputs("\033[0m", stdout);
}

static void ansi_putc(Display *d, char c, uint8_t attr) {
	(void)d;
	if(attr) {
		ansi_set_color(attr);
		fputc(c, stdout);
		ansi_reset_color();
	} else {
		fputc(c, stdout);
	}
	fflush(stdout);		// 实时输出
}

static void ansi_puts(Display *d, const char *s, uint8_t attr) {
	(void)d;
	if(attr) {
		ansi_set_color(attr);
		fputs(s, stdout);
		ansi_reset_color();
	} else {
		fputs(s, stdout);
	}
	fflush(stdout);
}

// 帧缓冲 → 亮度字符画（限制尺寸防止刷屏）
static void ansi_flush(Display *d) {
	if(d->width==0 || d->height==0) return;
	if(d->width > 200 || d->height > 60) {
		printf("\033[2J\033[H[dsp flush %lux%lu]\n",
			(unsigned long)d->width, (unsigned long)d->height);
		return;
	}
	fputs("\033[2J\033[H", stdout);		// 清屏 + 光标归位
	const char *ramp = " .:-=+*#%@";
	for(uint32_t y=0; y<d->height; y++) {
		for(uint32_t x=0; x<d->width; x++) {
			Display_pixel p=d->fb[y*d->width+x];
			uint32_t lum=((uint32_t)p.r*77 + p.g*150 + p.b*29) >> 8;	// 0..255
			putchar(ramp[lum*9/256]);
		}
		putchar('\n');
	}
	fflush(stdout);
}

// ==================== PPM 后端 ====================

static void ppm_putc(Display *d, char c, uint8_t attr) {
	(void)d; (void)c; (void)attr;		// 图像后端忽略字符
}

static void ppm_puts(Display *d, const char *s, uint8_t attr) {
	(void)d; (void)s; (void)attr;
}

static void ppm_flush(Display *d) {
	Ppm_priv *p=(Ppm_priv *)d->priv;
	if(!p || !p->fp) return;
	FILE *fp=p->fp;
	rewind(fp);
	fprintf(fp, "P6\n%u %u\n255\n", d->width, d->height);
	fwrite(d->fb, sizeof(Display_pixel), (size_t)d->width*d->height, fp);
	fflush(fp);
}

// ==================== FB（内存）后端 ====================

static void fb_putc(Display *d, char c, uint8_t attr) {
	(void)d; (void)c; (void)attr;		// 像素后端忽略字符
}

static void fb_puts(Display *d, const char *s, uint8_t attr) {
	(void)d; (void)s; (void)attr;
}

static void fb_flush(Display *d) {
	(void)d;		// 帧缓冲已在内存，无需渲染
}

// ==================== 通用接口 ====================

void display_init(Display *d, Display_backend backend, uint32_t width, uint32_t height) {
	if(!d) return;
	memset(d, 0, sizeof(*d));
	d->backend = backend;
	d->name = display_backend_name(backend);
	d->width = width;
	d->height = height;
	d->fb = calloc((size_t)width*height, sizeof(Display_pixel));
	if(!d->fb) {
		fprintf(stderr, "FATAL: display 帧缓冲分配失败\n");
		exit(1);
	}
	switch(backend) {
		case DISPLAY_BACKEND_PPM: {
			Ppm_priv *p=calloc(1, sizeof(Ppm_priv));
			snprintf(p->path, sizeof(p->path), "%s", g_ppm_path);
			p->fp=fopen(p->path, "w+");
			if(!p->fp) {
				fprintf(stderr, "FATAL: 无法打开 PPM 输出文件: %s\n", p->path);
				exit(1);
			}
			d->priv=p;
			break;
		}
		default:
			d->priv=NULL;
			break;
	}
}

void display_destroy(Display *d) {
	if(!d) return;
	if(d->priv) {
		Ppm_priv *p=(Ppm_priv *)d->priv;
		if(p->fp) fclose(p->fp);
		free(p);
	}
	free(d->fb);
	memset(d, 0, sizeof(*d));
}

void display_putc(Display *d, char c, uint8_t attr) {
	if(!d) return;
	switch(d->backend) {
		case DISPLAY_BACKEND_TTY:  tty_putc(d, c, attr);  break;
		case DISPLAY_BACKEND_ANSI: ansi_putc(d, c, attr); break;
		case DISPLAY_BACKEND_PPM:  ppm_putc(d, c, attr);  break;
		case DISPLAY_BACKEND_FB:   fb_putc(d, c, attr);   break;
		default: break;
	}
}

void display_puts(Display *d, const char *s, uint8_t attr) {
	if(!d || !s) return;
	switch(d->backend) {
		case DISPLAY_BACKEND_TTY:  tty_puts(d, s, attr);  break;
		case DISPLAY_BACKEND_ANSI: ansi_puts(d, s, attr); break;
		case DISPLAY_BACKEND_PPM:  ppm_puts(d, s, attr);  break;
		case DISPLAY_BACKEND_FB:   fb_puts(d, s, attr);   break;
		default: break;
	}
}

void display_clear(Display *d, Display_pixel color) {
	if(!d) return;
	for(uint32_t i=0; i<d->width*d->height; i++) d->fb[i]=color;
}

void display_set_pixel(Display *d, uint32_t x, uint32_t y, Display_pixel c) {
	if(!d || x>=d->width || y>=d->height) return;
	d->fb[y*d->width+x]=c;
}

void display_blit_indexed(Display *d, const uint8_t *indexed,
                          const uint8_t *palette_rgb, uint32_t palette_entries) {
	if(!d || !indexed || !palette_rgb) return;
	for(uint32_t i=0; i<d->width*d->height; i++) {
		uint8_t idx=indexed[i];
		if(idx < palette_entries) {
			d->fb[i].r=palette_rgb[idx*3+0];
			d->fb[i].g=palette_rgb[idx*3+1];
			d->fb[i].b=palette_rgb[idx*3+2];
		} else {
			d->fb[i].r=d->fb[i].g=d->fb[i].b=0;
		}
	}
}

void display_flush(Display *d) {
	if(!d) return;
	switch(d->backend) {
		case DISPLAY_BACKEND_TTY:  tty_flush(d);  break;
		case DISPLAY_BACKEND_ANSI: ansi_flush(d); break;
		case DISPLAY_BACKEND_PPM:  ppm_flush(d);  break;
		case DISPLAY_BACKEND_FB:   fb_flush(d);   break;
		default: break;
	}
}

Display_pixel display_get_pixel(Display *d, uint32_t x, uint32_t y) {
	Display_pixel black={0,0,0};
	if(!d || x>=d->width || y>=d->height) return black;
	return d->fb[y*d->width+x];
}
