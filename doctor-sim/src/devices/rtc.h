// rtc.h - RTC 实时时钟/日历设备（见 dev_specification.md 第 6 节）
// 端口:
//   0x30 RTC_SEC        8位  秒 0-59
//   0x31 RTC_MIN        8位  分 0-59
//   0x32 RTC_HOUR       8位  时 0-23
//   0x33 RTC_DAY        8位  日 1-31
//   0x34 RTC_MONTH      8位  月 1-12
//   0x35 RTC_YEAR      16位  年 0-9999
//   0x36 RTC_CTRL       8位  控制寄存器
//   0x37 RTC_STATUS     8位  状态寄存器（W1C）
//   0x38 RTC_ALARM_SEC  8位  闹钟秒
//   0x39 RTC_ALARM_MIN  8位  闹钟分
//   0x3A RTC_ALARM_HOUR 8位  闹钟时
//   0x3B RTC_ALARM_CTRL 8位  闹钟比较使能
//   0x3C RTC_PERIOD     8位  周期中断间隔（秒）
//   0x3D RTC_RESET      只写  复位

#ifndef RTC_H
#define RTC_H

#include "../device.h"
#include <stdbool.h>
#include <stdint.h>

struct Dev_RTC_t;
typedef struct Dev_RTC_t Dev_RTC;

struct Dev_RTC_t {
uint8_t sec;
uint8_t min;
uint8_t hour;
uint8_t day;
uint8_t month;
uint16_t year;

uint8_t ctrl;// 0x36
uint8_t status;// 0x37
uint8_t alarm_sec;// 0x38
uint8_t alarm_min;// 0x39
uint8_t alarm_hour;// 0x3A
uint8_t alarm_ctrl;// 0x3B
uint8_t period;// 0x3C

uint8_t period_counter;// 距下一次周期中断的剩余秒数
bool prev_alarm_match;// 上一次闹钟匹配状态（边沿检测）

uint64_t last_time_ns;// 墙钟模式：上次采样时间
uint64_t accu_ns;// 墙钟模式：未消费的纳秒余数
};

void rtc_init(Device *dev);
void rtc_tick(Device *dev, uint64_t d_tick);
uint32_t rtc_read_port(Device *dev, uint16_t port, uint8_t size);
void rtc_write_port(Device *dev, uint16_t port, uint32_t data, uint8_t size);
void rtc_reset(Device *dev);
void rtc_dump(Device *dev);
void rtc_destroy(Device *dev);

static const Device_ops rtc_ops = {
.init = rtc_init,
.tick = rtc_tick,
.read_port = rtc_read_port,
.write_port = rtc_write_port,
.reset = rtc_reset,
.dump = rtc_dump,
.destroy = rtc_destroy,
};

#define RTC_BASE_PORT 0x30
#define RTC_PORT_CNT  14

#define RTC_PORT_SEC        0x30
#define RTC_PORT_MIN        0x31
#define RTC_PORT_HOUR       0x32
#define RTC_PORT_DAY        0x33
#define RTC_PORT_MONTH      0x34
#define RTC_PORT_YEAR       0x35
#define RTC_PORT_CTRL       0x36
#define RTC_PORT_STATUS     0x37
#define RTC_PORT_ALARM_SEC  0x38
#define RTC_PORT_ALARM_MIN  0x39
#define RTC_PORT_ALARM_HOUR 0x3A
#define RTC_PORT_ALARM_CTRL 0x3B
#define RTC_PORT_PERIOD     0x3C
#define RTC_PORT_RESET      0x3D

#define RTC_IRQ 2

#define RTC_CTRL_EN    0x01
#define RTC_CTRL_PIE   0x02
#define RTC_CTRL_AIE   0x04
#define RTC_CTRL_HALT  0x08
#define RTC_CTRL_TM    0x10

#define RTC_ST_INV  0x01
#define RTC_ST_AF   0x02
#define RTC_ST_PF   0x04

#define RTC_ALARM_SEC_EN  0x01
#define RTC_ALARM_MIN_EN  0x02
#define RTC_ALARM_HOUR_EN 0x04

#endif
