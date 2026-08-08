#include "device.h"
#include "cpu.h"
#include <stdint.h>

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
uint32_t device_read(DOCTOR_CPU *cpu, uint16_t port, uint8_t size) {
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

