#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct DOCTOR_CPU_t DOCTOR_CPU;

typedef enum {
	DEVICE_TYPE_PIT=0,							// PIT
	DEVICE_TYPE_UART,							// 串口
	DEVICE_TYPE_KBC,							// 键盘
	DEVICE_TYPE_FB,								// 帧缓冲
	DEVICE_TYPE_RTC,							// 时钟
	DEVICE_TYPE_DISK,							// 块设备
	DEVICE_TYPE_UNKNOWN,
} Device_type;

struct Device_t;
typedef struct Device_t Device;
struct Device_ops_t;
typedef struct Device_ops_t Device_ops;
struct Device_mgr_t;
typedef struct Device_mgr_t Device_mgr;

struct Device_ops_t {
	void (*init)(Device *dev);					// 设备初始化
	void (*tick)(Device *dev, uint64_t d_tick);	// 每一轮都要调用的函数

	uint32_t (*read_port)(Device *dev,			// 从指定端口读取
			uint16_t port, uint8_t size);
	void (*write_port)(Device *dev,				// 从指定端口写入
			uint16_t port, uint32_t data, uint8_t size);

	void (*reset)(Device *dev);					// 重置
	void (*dump)(Device *dev);					// 打印设备状态
	void (*destroy)(Device *dev);				// 释放私有数据
};

struct Device_t {								// 设备结构体
	Device_type type;	
	const char *name;

	uint16_t base_port;							// 端口基地址
	uint8_t port_cnt;							// 端口数量
	
	uint32_t base_mem;							// 内存映射基址
	uint32_t end_mem;							// 内存映射结尾
	bool is_mmio;								// 启用MMIO. 启用时，映射空间为[base_mem, end_mem]

	uint8_t irq;								// IRQ
	void *private_data;							// 私有数据
	const Device_ops *ops;						// 操作

	DOCTOR_CPU *cpu;

	Device *next;
};

struct Device_mgr_t {
	Device *head;
	int count;
	uint64_t total_cycles;

	bool last_dev_not_found;					// 上一次查找时没有找到设备
};

void device_register(DOCTOR_CPU *cpu, Device *dev);
Device *device_find(DOCTOR_CPU *cpu, uint16_t port);
uint32_t device_read(DOCTOR_CPU *cpu, uint16_t port, uint8_t size);
void device_write(DOCTOR_CPU *cpu, uint16_t port, uint32_t data, uint8_t size);
void device_tick_all(DOCTOR_CPU *cpu, uint64_t cycles);
void device_reset_all(DOCTOR_CPU *cpu);
void device_dump_all(DOCTOR_CPU *cpu);
void device_destroy_all(DOCTOR_CPU *cpu);

#endif

