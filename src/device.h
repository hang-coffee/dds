#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>

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
	void (*tick)(Device *dev);					// 每一轮都要调用的函数

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
	
	Device *next;
};

struct Device_mgr_t {
	Device *head;
	int count;
	uint64_t total_cycles;
};

void device_register(DOCTOR_CPU *cpu, Device *dev) {
    if (!cpu || !dev) return;
    dev->next = cpu->dev_mgr.head;
    cpu->dev_mgr.head = dev;
    cpu->dev_mgr.count++;
    if (dev->ops && dev->ops->init)   // 注册时立即初始化
        dev->ops->init(dev);
}
Device *device_find(DOCTOR_CPU *cpu, uint16_t port) {
    if (!cpu) return NULL;
    Device *cur = cpu->dev_mgr.head;
    while (cur) {
        // 防止 base_port=0xFFFF + port_cnt=2 时溢出
        if (cur->port_cnt > 0 &&
            (uint32_t)port >= cur->base_port &&
            (uint32_t)port < (uint32_t)cur->base_port + cur->port_cnt) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}
static inline uint32_t device_read(DOCTOR_CPU *cpu, uint16_t port, uint8_t size) {
    Device *dev = device_find(cpu, port);
    if (dev && dev->ops && dev->ops->read_port) {
        return dev->ops->read_port(dev, port, size);
    }
    // 无设备时返回全1
    switch (size) {
        case 1: return 0xFF;        // BYTE
        case 2: return 0xFFFF;      // WORD
        case 3: return 0xFFFFFFFF;  // DWORD
        default: return 0xFFFFFFFF;
    }
}
void device_write(DOCTOR_CPU *cpu, uint16_t port, uint32_t data, uint8_t size) {
    Device *dev = device_find(cpu, port);
    if (dev && dev->ops && dev->ops->write_port) {
        dev->ops->write_port(dev, port, data, size);
    }
    // 无设备忽略
}
void device_tick_all(DOCTOR_CPU *cpu, uint64_t cycles) {
    if (!cpu) return;

    cpu->dev_mgr.total_cycles += cycles;

    Device *cur = cpu->dev_mgr.head;
    while (cur) {
        Device *next = cur->next;          // 提前保存
        if (cur->ops && cur->ops->tick)
            cur->ops->tick(cur);
        cur = next;
    }
}
void device_reset_all(DOCTOR_CPU *cpu) {
    if (!cpu) return;

    Device *dev = cpu->dev_mgr.head;
    while (dev) {
        if (dev->ops && dev->ops->reset)
            dev->ops->reset(dev);
        dev = dev->next;
    }
}
void device_dump_all(DOCTOR_CPU *cpu) {
    if (!cpu) return;

    Device *dev = cpu->dev_mgr.head;
    while (dev) {
        if (dev->ops && dev->ops->dump)
            dev->ops->dump(dev);
        dev = dev->next;
    }
}
void device_destroy_all(DOCTOR_CPU *cpu) {
    if (!cpu) return;

    Device *dev = cpu->dev_mgr.head;
    while (dev) {
        Device *next = dev->next;  // 先记下下一个节点
        if (dev->ops && dev->ops->destroy)
            dev->ops->destroy(dev);
        dev = next;
    }

    cpu->dev_mgr.head = NULL;
    cpu->dev_mgr.count = 0;
    cpu->dev_mgr.total_cycles = 0;
}
#endif

