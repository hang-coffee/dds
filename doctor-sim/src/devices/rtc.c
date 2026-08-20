// rtc.c - RTC 实时时钟/日历设备
// 见 dev_specification.md 第 6 节。
//
// 时间推进：
//   - CTRL.TM=0：宿主墙钟模式，按真实时间自动推进。
//   - CTRL.TM=1：手动模式，时间仅通过软件写端口改变。
//
// 中断：
//   - 周期中断：每个 RTC_PERIOD 秒触发 IRQ2。
//   - 闹钟中断：时间从非匹配变为匹配时触发一次 IRQ2。

#include "rtc.h"
#include "../interrupt.h"
#include "../cpu.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static inline uint64_t rtc_time2ns(struct timespec now) {
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static void rtc_load_host_time(Dev_RTC *r) {
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    r->sec   = (uint8_t)tmv.tm_sec;
    r->min   = (uint8_t)tmv.tm_min;
    r->hour  = (uint8_t)tmv.tm_hour;
    r->day   = (uint8_t)tmv.tm_mday;
    r->month = (uint8_t)(tmv.tm_mon + 1);
    r->year  = (uint16_t)(tmv.tm_year + 1900);
}

static bool rtc_is_leap(uint16_t year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static uint8_t rtc_days_in_month(uint16_t year, uint8_t month) {
    switch (month) {
        case 1: case 3: case 5: case 7: case 8: case 10: case 12:
            return 31;
        case 4: case 6: case 9: case 11:
            return 30;
        case 2:
            return rtc_is_leap(year) ? 29 : 28;
        default:
            return 0;
    }
}

static bool rtc_date_valid(uint16_t year, uint8_t month, uint8_t day) {
    if (year > 9999) return false;
    if (month < 1 || month > 12) return false;
    uint8_t dim = rtc_days_in_month(year, month);
    if (dim == 0 || day < 1 || day > dim) return false;
    return true;
}

static bool rtc_time_valid(uint8_t sec, uint8_t min, uint8_t hour) {
    return sec <= 59 && min <= 59 && hour <= 23;
}

static void rtc_add_one_second(Dev_RTC *r) {
    r->sec++;
    if (r->sec < 60) return;
    r->sec = 0;
    r->min++;
    if (r->min < 60) return;
    r->min = 0;
    r->hour++;
    if (r->hour < 24) return;
    r->hour = 0;
    r->day++;
    if (r->day <= rtc_days_in_month(r->year, r->month)) return;
    r->day = 1;
    r->month++;
    if (r->month <= 12) return;
    r->month = 1;
    r->year++;
    if (r->year > 9999) r->year = 0;    // 溢出回绕，保持字段合法
}

static void rtc_add_seconds(Dev_RTC *r, uint64_t seconds) {
    while (seconds-- > 0) {
        rtc_add_one_second(r);
    }
}

// 当前时间是否满足闹钟匹配
static bool rtc_alarm_matches(const Dev_RTC *r) {
    if (r->alarm_ctrl == 0) return false;
    if ((r->alarm_ctrl & RTC_ALARM_SEC_EN) && r->sec != r->alarm_sec) return false;
    if ((r->alarm_ctrl & RTC_ALARM_MIN_EN) && r->min != r->alarm_min) return false;
    if ((r->alarm_ctrl & RTC_ALARM_HOUR_EN) && r->hour != r->alarm_hour) return false;
    return true;
}

// 更新闹钟状态，返回是否产生了新的边沿触发
static bool rtc_update_alarm(Device *dev, Dev_RTC *r) {
    bool match = rtc_alarm_matches(r);
    bool triggered = false;
    if (match && !r->prev_alarm_match) {
        r->status |= RTC_ST_AF;
        if ((r->ctrl & RTC_CTRL_AIE) && dev->cpu) {
            irq_set(dev->cpu, RTC_IRQ);
        }
        triggered = true;
    }
    r->prev_alarm_match = match;
    return triggered;
}

// 软件修改时间/闹钟/控制后，重新同步闹钟边沿状态但不触发中断
static void rtc_recalc_alarm(Dev_RTC *r) {
    r->prev_alarm_match = rtc_alarm_matches(r);
}

static void rtc_handle_period(Device *dev, Dev_RTC *r) {
    if (!(r->ctrl & RTC_CTRL_EN) || (r->ctrl & RTC_CTRL_HALT) || !(r->ctrl & RTC_CTRL_PIE)) {
        return;
    }
    uint8_t interval = r->period ? r->period : 1;
    if (r->period_counter == 0) {
        r->period_counter = interval;
    }
    r->period_counter--;
    if (r->period_counter == 0) {
        r->status |= RTC_ST_PF;
        if (dev->cpu) irq_set(dev->cpu, RTC_IRQ);
        r->period_counter = interval;
    }
}

void rtc_init(Device *dev) {
    Dev_RTC *priv = (Dev_RTC *)calloc(1, sizeof(Dev_RTC));
    if (!priv) {
        perror("Failed to calloc for RTC");
        exit(EXIT_FAILURE);
    }
    dev->type = DEVICE_TYPE_RTC;
    dev->name = "RTC";
    dev->base_port = RTC_BASE_PORT;
    dev->port_cnt = RTC_PORT_CNT;
    dev->is_mmio = false;
    dev->irq = RTC_IRQ;
    dev->ops = &rtc_ops;
    dev->private_data = priv;

    // 复位默认 CTRL=0（TM=0，宿主墙钟源），时间初始化为宿主本地时间
    rtc_load_host_time(priv);
    priv->period_counter = 1;
    priv->prev_alarm_match = false;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    priv->last_time_ns = rtc_time2ns(now);
    priv->accu_ns = 0;
}

void rtc_tick(Device *dev, uint64_t d_tick) {
    if (!dev->private_data || !dev->cpu) return;
    Dev_RTC *r = (Dev_RTC *)dev->private_data;

    if (!(r->ctrl & RTC_CTRL_EN) || (r->ctrl & RTC_CTRL_HALT)) {
        return;
    }

    uint64_t elapsed_seconds = 0;
    if (!(r->ctrl & RTC_CTRL_TM)) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t now_ns = rtc_time2ns(now);
        r->accu_ns += now_ns - r->last_time_ns;
        r->last_time_ns = now_ns;
        elapsed_seconds = r->accu_ns / 1000000000ULL;
        r->accu_ns %= 1000000000ULL;
    } else {
        // 手动模式：不自动推进时间
        elapsed_seconds = 0;
    }

    if (elapsed_seconds > 0) {
        rtc_add_seconds(r, elapsed_seconds);
    }

    // 周期中断基于已推进的秒数
    if ((r->ctrl & RTC_CTRL_PIE) && elapsed_seconds > 0) {
        for (uint64_t i = 0; i < elapsed_seconds; i++) {
            rtc_handle_period(dev, r);
        }
    }

    // 闹钟匹配在时间变化后检查
    if (elapsed_seconds > 0) {
        rtc_update_alarm(dev, r);
    }

    (void)d_tick;
}

static uint8_t rtc_read8(const Dev_RTC *r, uint8_t reg) {
    switch (reg) {
        case RTC_PORT_SEC:        return r->sec;
        case RTC_PORT_MIN:        return r->min;
        case RTC_PORT_HOUR:       return r->hour;
        case RTC_PORT_DAY:        return r->day;
        case RTC_PORT_MONTH:      return r->month;
        case RTC_PORT_CTRL:       return r->ctrl;
        case RTC_PORT_STATUS:     return r->status;
        case RTC_PORT_ALARM_SEC:  return r->alarm_sec;
        case RTC_PORT_ALARM_MIN:  return r->alarm_min;
        case RTC_PORT_ALARM_HOUR: return r->alarm_hour;
        case RTC_PORT_ALARM_CTRL: return r->alarm_ctrl;
        case RTC_PORT_PERIOD:     return r->period;
        default:                  return 0xff;
    }
}

static uint32_t rtc_read_port_impl(Dev_RTC *r, uint16_t port, uint8_t size) {
    if (port == RTC_PORT_YEAR) {
        switch (size) {
            case 1: return r->year & 0xff;
            case 2: return r->year & 0xffff;
            default: return r->year & 0xffff;
        }
    }
    if (port == RTC_PORT_RESET) {
        return 0xff;    // 只写端口，读返回全 1
    }
    uint8_t v = rtc_read8(r, port);
    switch (size) {
        case 1: return v;
        case 2: return v;
        default: return v;
    }
}

uint32_t rtc_read_port(Device *dev, uint16_t port, uint8_t size) {
    if (!dev->private_data) return 0xffffffff;
    return rtc_read_port_impl((Dev_RTC *)dev->private_data, port, size);
}

static bool rtc_write8(Dev_RTC *r, uint16_t port, uint8_t value) {
    switch (port) {
        case RTC_PORT_SEC:
            if (!rtc_time_valid(value, r->min, r->hour) ||
                !rtc_date_valid(r->year, r->month, r->day)) {
                r->status |= RTC_ST_INV;
                return false;
            }
            r->sec = value;
            break;
        case RTC_PORT_MIN:
            if (!rtc_time_valid(r->sec, value, r->hour) ||
                !rtc_date_valid(r->year, r->month, r->day)) {
                r->status |= RTC_ST_INV;
                return false;
            }
            r->min = value;
            break;
        case RTC_PORT_HOUR:
            if (!rtc_time_valid(r->sec, r->min, value) ||
                !rtc_date_valid(r->year, r->month, r->day)) {
                r->status |= RTC_ST_INV;
                return false;
            }
            r->hour = value;
            break;
        case RTC_PORT_DAY:
            if (!rtc_time_valid(r->sec, r->min, r->hour) ||
                !rtc_date_valid(r->year, r->month, value)) {
                r->status |= RTC_ST_INV;
                return false;
            }
            r->day = value;
            break;
        case RTC_PORT_MONTH:
            if (!rtc_time_valid(r->sec, r->min, r->hour) ||
                !rtc_date_valid(r->year, value, r->day)) {
                r->status |= RTC_ST_INV;
                return false;
            }
            r->month = value;
            break;
        case RTC_PORT_CTRL:
            r->ctrl = value;
            break;
        case RTC_PORT_STATUS:
            // W1C：写 1 清除
            if (value & RTC_ST_INV) r->status &= (uint8_t)~RTC_ST_INV;
            if (value & RTC_ST_AF)  r->status &= (uint8_t)~RTC_ST_AF;
            if (value & RTC_ST_PF)  r->status &= (uint8_t)~RTC_ST_PF;
            break;
        case RTC_PORT_ALARM_SEC:
        case RTC_PORT_ALARM_MIN:
        case RTC_PORT_ALARM_HOUR:
            // 闹钟字段只做范围校验，不校验与当前时间的关系
            if (port == RTC_PORT_ALARM_HOUR) {
                if (value > 23) {
                    r->status |= RTC_ST_INV;
                    return false;
                }
            } else if (value > 59) {
                r->status |= RTC_ST_INV;
                return false;
            }
            if (port == RTC_PORT_ALARM_SEC)  r->alarm_sec = value;
            if (port == RTC_PORT_ALARM_MIN)  r->alarm_min = value;
            if (port == RTC_PORT_ALARM_HOUR) r->alarm_hour = value;
            break;
        case RTC_PORT_ALARM_CTRL:
            r->alarm_ctrl = value & 0x07;
            break;
        case RTC_PORT_PERIOD:
            r->period = value;
            r->period_counter = value ? value : 1;
            break;
        default:
            return false;
    }

    rtc_recalc_alarm(r);
    return true;
}

void rtc_write_port(Device *dev, uint16_t port, uint32_t data, uint8_t size) {
    if (!dev->private_data) return;
    Dev_RTC *r = (Dev_RTC *)dev->private_data;

    if (port == RTC_PORT_RESET) {
        rtc_reset(dev);
        return;
    }

    if (port == RTC_PORT_YEAR) {
        uint16_t new_year = r->year;
        switch (size) {
            case 1: new_year = (uint16_t)((r->year & 0xff00) | (data & 0xff)); break;
            case 2: new_year = (uint16_t)(data & 0xffff); break;
            default: new_year = (uint16_t)(data & 0xffff); break;
        }
        if (new_year > 9999 || !rtc_date_valid(new_year, r->month, r->day)) {
            r->status |= RTC_ST_INV;
            return;
        }
        r->year = new_year;
        rtc_recalc_alarm(r);
        return;
    }

    uint8_t value = (uint8_t)(data & 0xff);
    rtc_write8(r, port, value);
}

void rtc_reset(Device *dev) {
    if (!dev->private_data) return;
    Dev_RTC *r = (Dev_RTC *)dev->private_data;

    memset(r, 0, sizeof(*r));
    rtc_load_host_time(r);
    r->period_counter = 1;
    r->prev_alarm_match = false;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    r->last_time_ns = rtc_time2ns(now);
    r->accu_ns = 0;
}

void rtc_dump(Device *dev) {
    if (!dev->private_data) return;
    Dev_RTC *r = (Dev_RTC *)dev->private_data;
    fprintf(stderr, "INFO: RTC: %04u-%02u-%02u %02u:%02u:%02u ctrl=%02X status=%02X\n",
        r->year, r->month, r->day, r->hour, r->min, r->sec,
        r->ctrl, r->status);
}

void rtc_destroy(Device *dev) {
    if (dev->private_data) {
        free(dev->private_data);
        dev->private_data = NULL;
    }
}
