// disk.c - DISK 块设备
//
// 端口 I/O 模型，见 dev_specification.md 第 5 节。
// 支持两种数据通路：
//   - BUF_ADDR != 0：内存批量传输（READ/WRITE 直接搬数据空间）
//   - BUF_ADDR == 0：PIO 模式，通过 DATA 端口逐次读写
//
// 命令同步完成：写 CMD 后立即执行；状态位仍按规范给出。

#include "disk.h"
#include "../interrupt.h"
#include "../cpu.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *g_disk_path = NULL;

void disk_set_image_path(const char *path) {
	g_disk_path = path;
}

// ---------- 内部工具 ----------

static uint32_t disk_reg_read32(Dev_DISK *d, uint32_t value, uint8_t size) {
	(void)d;
	switch (size) {
		case 1: return value & 0xff;
		case 2: return value & 0xffff;
		default: return value;
	}
}

static void disk_reg_write32(uint32_t *reg, uint32_t data, uint8_t size) {
	switch (size) {
		case 1: *reg = (*reg & 0xffffff00u) | (data & 0xffu); break;
		case 2: *reg = (*reg & 0xffff0000u) | (data & 0xffffu); break;
		default: *reg = data; break;
	}
}

static uint64_t disk_lba(const Dev_DISK *d) {
	return ((uint64_t)d->lba_hi << 32) | d->lba_lo;
}

static bool disk_ready(const Dev_DISK *d) {
	return (d->cfg & DISK_CFG_EN) != 0 && d->image != NULL && d->num_sectors > 0;
}

static void disk_clear_irq(Device *dev, Dev_DISK *d) {
	if (d->status & DISK_ST_IRQ) {
		d->status &= (uint8_t)~DISK_ST_IRQ;
		if (dev->cpu) irq_clear(dev->cpu, DISK_IRQ);
	}
}

static void disk_raise_irq(Device *dev, Dev_DISK *d) {
	d->status |= DISK_ST_IRQ;
	if ((d->cfg & DISK_CFG_IE) && dev->cpu) {
		irq_set(dev->cpu, DISK_IRQ);
	}
}

// 把内存镜像写回磁盘文件
static void disk_flush(Dev_DISK *d) {
	if (!d->path || !d->image || d->image_size == 0) return;
	FILE *fp = fopen(d->path, "wb");
	if (!fp) return;
	fwrite(d->image, 1, d->image_size, fp);
	fclose(fp);
}

static void disk_free_pio(Dev_DISK *d) {
	free(d->pio_buf);
	d->pio_buf = NULL;
	d->pio_len = 0;
	d->pio_off = 0;
	d->pio_write = false;
}

static int disk_load_image(Dev_DISK *d, const char *path) {
	FILE *fp = fopen(path, "rb");
	if (!fp) return -1;
	if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
	long sz = ftell(fp);
	if (sz <= 0) { fclose(fp); return -1; }
	rewind(fp);
	uint8_t *buf = (uint8_t *)malloc((size_t)sz);
	if (!buf) { fclose(fp); return -1; }
	size_t rd = fread(buf, 1, (size_t)sz, fp);
	fclose(fp);
	if (rd != (size_t)sz) { free(buf); return -1; }
	d->image = buf;
	d->image_size = (size_t)sz;
	d->num_sectors = (uint32_t)((size_t)sz / DISK_SECTOR_SIZE);
	if (d->num_sectors == 0) {
		// 小于一个扇区的镜像视为无有效介质
		free(buf);
		d->image = NULL;
		d->image_size = 0;
		return -1;
	}
	if (d->path) free(d->path);
	d->path = strdup(path);
	return 0;
}

// 内存批量传输；返回 0 成功，-1 失败
static int disk_mem_transfer(Device *dev, Dev_DISK *d, bool is_write) {
	if (!disk_ready(d)) return -1;
	uint64_t start = disk_lba(d);
	uint64_t sectors = d->count;
	if (sectors == 0) return 0;
	if (start + sectors > d->num_sectors) return -1;

	uint64_t byte_off = start * DISK_SECTOR_SIZE;
	uint64_t byte_len = sectors * DISK_SECTOR_SIZE;
	uint64_t buf_end = (uint64_t)d->buf_addr + byte_len;
	if (buf_end > DATA_SIZE) return -1;

	uint8_t *mem = dev->cpu->data_mem + d->buf_addr;
	uint8_t *img = d->image + byte_off;
	if (is_write) {
		memcpy(img, mem, (size_t)byte_len);
		disk_flush(d);
	} else {
		memcpy(mem, img, (size_t)byte_len);
	}
	return 0;
}

// PIO 读/写命令初始化
static int disk_pio_begin(Device *dev, Dev_DISK *d, bool is_write) {
	(void)dev;
	if (!disk_ready(d)) return -1;
	uint64_t start = disk_lba(d);
	uint64_t sectors = d->count;
	if (sectors == 0) return 0;
	if (start + sectors > d->num_sectors) return -1;

	uint64_t byte_len = sectors * DISK_SECTOR_SIZE;
	if (byte_len == 0) return 0;

	disk_free_pio(d);
	d->pio_buf = (uint8_t *)malloc((size_t)byte_len);
	if (!d->pio_buf) return -1;
	d->pio_len = (size_t)byte_len;
	d->pio_off = 0;
	d->pio_write = is_write;

	if (is_write) {
		memset(d->pio_buf, 0, (size_t)byte_len);
	} else {
		memcpy(d->pio_buf, d->image + start * DISK_SECTOR_SIZE, (size_t)byte_len);
	}

	d->status &= (uint8_t)~DISK_ST_BSY;
	d->status |= DISK_ST_DRQ;
	return 0;
}

// PIO 传输结束（读缓冲已读完 / 写缓冲已收满）
static void disk_pio_finish(Device *dev, Dev_DISK *d) {
	if (d->pio_write) {
		uint64_t start = disk_lba(d);
		memcpy(d->image + start * DISK_SECTOR_SIZE, d->pio_buf, d->pio_len);
		disk_flush(d);
	}
	disk_free_pio(d);
	d->status &= (uint8_t)~DISK_ST_DRQ;
	disk_raise_irq(dev, d);
}

static void disk_do_command(Device *dev, Dev_DISK *d, uint8_t cmd) {
	d->status &= (uint8_t)~(DISK_ST_ERR | DISK_ST_IRQ | DISK_ST_DRQ | DISK_ST_BSY);
	disk_free_pio(d);

	if ((d->cfg & DISK_CFG_EN) == 0 && cmd != DISK_CMD_RESET) {
		d->status |= DISK_ST_ERR;
		disk_raise_irq(dev, d);
		return;
	}

	switch (cmd) {
		case DISK_CMD_NOP:
			disk_raise_irq(dev, d);
			break;
		case DISK_CMD_READ:
			d->status |= DISK_ST_BSY;
			if (d->buf_addr != 0) {
				int rc = disk_mem_transfer(dev, d, false);
				d->status &= (uint8_t)~DISK_ST_BSY;
				if (rc != 0) d->status |= DISK_ST_ERR;
				disk_raise_irq(dev, d);
			} else {
				if (disk_pio_begin(dev, d, false) != 0) {
					d->status |= DISK_ST_ERR;
					disk_raise_irq(dev, d);
				} else if (!(d->status & DISK_ST_DRQ)) {
					// COUNT=0 或空传输：立即完成
					d->status &= (uint8_t)~DISK_ST_BSY;
					disk_raise_irq(dev, d);
				}
			}
			break;
		case DISK_CMD_WRITE:
			d->status |= DISK_ST_BSY;
			if (d->buf_addr != 0) {
				int rc = disk_mem_transfer(dev, d, true);
				d->status &= (uint8_t)~DISK_ST_BSY;
				if (rc != 0) d->status |= DISK_ST_ERR;
				disk_raise_irq(dev, d);
			} else {
				if (disk_pio_begin(dev, d, true) != 0) {
					d->status |= DISK_ST_ERR;
					disk_raise_irq(dev, d);
				} else if (!(d->status & DISK_ST_DRQ)) {
					// COUNT=0 或空传输：立即完成
					d->status &= (uint8_t)~DISK_ST_BSY;
					disk_raise_irq(dev, d);
				}
			}
			break;
		case DISK_CMD_RESET:
			d->data = 0;
			d->lba_lo = 0;
			d->lba_hi = 0;
			d->count = 0;
			d->status = 0;
			if (disk_ready(d)) d->status |= DISK_ST_RDY;
			disk_raise_irq(dev, d);
			break;
		default:
			d->status |= DISK_ST_ERR;
			disk_raise_irq(dev, d);
			break;
	}
}

// ---------- 设备接口 ----------

void disk_init(Device *dev) {
	Dev_DISK *priv = (Dev_DISK *)calloc(1, sizeof(Dev_DISK));
	if (!priv) {
		perror("Failed to calloc for DISK");
		exit(EXIT_FAILURE);
	}
	dev->type = DEVICE_TYPE_DISK;
	dev->name = "DISK";
	dev->base_port = DISK_BASE_PORT;
	dev->port_cnt = DISK_PORT_CNT;
	dev->is_mmio = false;
	dev->irq = DISK_IRQ;
	dev->ops = &disk_ops;
	dev->private_data = priv;

	if (g_disk_path && g_disk_path[0]) {
		if (disk_load_image(priv, g_disk_path) != 0) {
			fprintf(stderr, "WARNING: 无法加载 DISK 镜像: %s\n", g_disk_path);
		}
	}
	if (disk_ready(priv)) priv->status |= DISK_ST_RDY;
	return;
}

void disk_tick(Device *dev, uint64_t d_tick) {
	(void)dev;
	(void)d_tick;
	return;
}

uint32_t disk_read_port(Device *dev, uint16_t port, uint8_t size) {
	if (!dev || !dev->private_data) return 0xffffffff;
	Dev_DISK *d = (Dev_DISK *)dev->private_data;
	uint32_t val = 0;
	switch (port) {
		case DISK_PORT_DATA:
			if ((d->status & DISK_ST_DRQ) && !d->pio_write && d->pio_buf && d->pio_off < d->pio_len) {
				uint32_t width = (size == 1) ? 1 : (size == 2 ? 2 : 4);
				if (d->pio_off + width > d->pio_len) width = (uint32_t)(d->pio_len - d->pio_off);
				for (uint32_t i = 0; i < width; i++) {
					val |= ((uint32_t)d->pio_buf[d->pio_off + i]) << (i * 8);
				}
				d->pio_off += width;
				if (d->pio_off >= d->pio_len) disk_pio_finish(dev, d);
			}
			return disk_reg_read32(d, val, size);
		case DISK_PORT_LBA_LO:
			return disk_reg_read32(d, d->lba_lo, size);
		case DISK_PORT_LBA_HI:
			return disk_reg_read32(d, d->lba_hi, size);
		case DISK_PORT_COUNT:
			return disk_reg_read32(d, d->count, size);
		case DISK_PORT_CMD:
			return 0;
		case DISK_PORT_STATUS: {
			uint32_t st = disk_reg_read32(d, d->status, size);
			disk_clear_irq(dev, d);
			return st;
		}
		case DISK_PORT_BUF_ADDR:
			return disk_reg_read32(d, d->buf_addr, size);
		case DISK_PORT_CFG:
			return disk_reg_read32(d, d->cfg, size);
		case DISK_PORT_SECTOR_SIZE:
			return disk_reg_read32(d, DISK_SECTOR_SIZE, size);
		case DISK_PORT_NUM_SECTORS:
			return disk_reg_read32(d, d->num_sectors, size);
		default:
			return 0;
	}
}

void disk_write_port(Device *dev, uint16_t port, uint32_t data, uint8_t size) {
	if (!dev || !dev->private_data) return;
	Dev_DISK *d = (Dev_DISK *)dev->private_data;
	switch (port) {
		case DISK_PORT_DATA:
			if ((d->status & DISK_ST_DRQ) && d->pio_write && d->pio_buf && d->pio_off < d->pio_len) {
				uint32_t width = (size == 1) ? 1 : (size == 2 ? 2 : 4);
				if (d->pio_off + width > d->pio_len) width = (uint32_t)(d->pio_len - d->pio_off);
				for (uint32_t i = 0; i < width; i++) {
					d->pio_buf[d->pio_off + i] = (uint8_t)(data >> (i * 8));
				}
				d->pio_off += width;
				if (d->pio_off >= d->pio_len) disk_pio_finish(dev, d);
			}
			break;
		case DISK_PORT_LBA_LO:
			disk_reg_write32(&d->lba_lo, data, size);
			break;
		case DISK_PORT_LBA_HI:
			disk_reg_write32(&d->lba_hi, data, size);
			break;
		case DISK_PORT_COUNT:
			disk_reg_write32(&d->count, data, size);
			break;
		case DISK_PORT_CMD:
			if (!(d->status & DISK_ST_BSY)) disk_do_command(dev, d, (uint8_t)(data & 0xff));
			break;
		case DISK_PORT_STATUS:
			// 只读；写忽略
			break;
		case DISK_PORT_BUF_ADDR:
			disk_reg_write32(&d->buf_addr, data, size);
			break;
		case DISK_PORT_CFG:
			disk_reg_write32(&d->cfg, data, size);
			if (disk_ready(d)) d->status |= DISK_ST_RDY;
			else d->status &= (uint8_t)~DISK_ST_RDY;
			break;
		default:
			break;
	}
	return;
}

void disk_reset(Device *dev) {
	if (!dev || !dev->private_data) return;
	Dev_DISK *d = (Dev_DISK *)dev->private_data;
	disk_free_pio(d);
	d->data = 0;
	d->lba_lo = 0;
	d->lba_hi = 0;
	d->count = 0;
	d->buf_addr = 0;
	d->cfg = 0;
	d->status = 0;
	if (disk_ready(d)) d->status |= DISK_ST_RDY;
	return;
}

void disk_dump(Device *dev) {
	if (!dev || !dev->private_data) return;
	Dev_DISK *d = (Dev_DISK *)dev->private_data;
	fprintf(stderr, "INFO: DISK: path=%s sectors=%u image_size=%zu status=%02X cfg=%08X\n",
		d->path ? d->path : "(none)", d->num_sectors, d->image_size, d->status, d->cfg);
}

void disk_destroy(Device *dev) {
	if (!dev || !dev->private_data) return;
	Dev_DISK *d = (Dev_DISK *)dev->private_data;
	disk_flush(d);
	disk_free_pio(d);
	free(d->image);
	free(d->path);
	free(d);
	dev->private_data = NULL;
	return;
}
