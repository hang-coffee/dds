// disk.h - DISK 块设备（见 dev_specification.md 第 5 节）
// 端口:
//   0x26 DATA        32位 PIO 数据端口
//   0x27 LBA_LO      32位 起始 LBA 低 32 位
//   0x28 LBA_HI      32位 起始 LBA 高 32 位
//   0x29 COUNT       32位 传输扇区数
//   0x2A CMD          8位 命令寄存器（只写）
//   0x2B STATUS       8位 状态寄存器（只读，读后清 IRQ）
//   0x2C BUF_ADDR    32位 内存传输缓冲区地址
//   0x2D CFG         32位 配置寄存器
//   0x2E SECTOR_SIZE 32位 只读，恒为 512
//   0x2F NUM_SECTORS 32位 只读，总扇区数

#ifndef DISK_H
#define DISK_H

#include "../device.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct Dev_DISK_t;
typedef struct Dev_DISK_t Dev_DISK;

struct Dev_DISK_t {
uint8_t *image;// 磁盘镜像内存（NULL = 无介质）
size_t image_size;// 镜像字节数
uint32_t num_sectors;// 总扇区数 = image_size / 512
char *path;// 镜像路径（用于写回）

// 寄存器
uint32_t data;
uint32_t lba_lo;
uint32_t lba_hi;
uint32_t count;
uint32_t buf_addr;
uint32_t cfg;
uint8_t status;

// PIO 内部缓冲
uint8_t *pio_buf;
size_t pio_len;
size_t pio_off;
bool pio_write;// true = PIO 写，false = PIO 读
};

void disk_init(Device *dev);
void disk_tick(Device *dev, uint64_t d_tick);
uint32_t disk_read_port(Device *dev, uint16_t port, uint8_t size);
void disk_write_port(Device *dev, uint16_t port, uint32_t data, uint8_t size);
void disk_reset(Device *dev);
void disk_dump(Device *dev);
void disk_destroy(Device *dev);

// 在 cpu_init 之前调用，设置要加载的磁盘镜像路径；NULL 表示无磁盘
void disk_set_image_path(const char *path);

static const Device_ops disk_ops={
.init=disk_init,
.tick=disk_tick,
.read_port=disk_read_port,
.write_port=disk_write_port,
.reset=disk_reset,
.dump=disk_dump,
.destroy=disk_destroy,
};

#define DISK_BASE_PORT 0x26
#define DISK_PORT_CNT  10

#define DISK_PORT_DATA        0x26
#define DISK_PORT_LBA_LO      0x27
#define DISK_PORT_LBA_HI      0x28
#define DISK_PORT_COUNT       0x29
#define DISK_PORT_CMD         0x2A
#define DISK_PORT_STATUS      0x2B
#define DISK_PORT_BUF_ADDR    0x2C
#define DISK_PORT_CFG         0x2D
#define DISK_PORT_SECTOR_SIZE 0x2E
#define DISK_PORT_NUM_SECTORS 0x2F

#define DISK_SECTOR_SIZE 512
#define DISK_IRQ 5

#define DISK_CFG_EN  0x00000001
#define DISK_CFG_IE  0x00000002

#define DISK_ST_BSY   0x01
#define DISK_ST_DRQ   0x02
#define DISK_ST_ERR   0x04
#define DISK_ST_IRQ   0x08
#define DISK_ST_WPROT 0x10
#define DISK_ST_RDY   0x20

#define DISK_CMD_NOP          0x00
#define DISK_CMD_READ         0x01
#define DISK_CMD_WRITE        0x02
#define DISK_CMD_RESET        0xFF

#endif
