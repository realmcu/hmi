#ifndef POSIX_IOCTL_RTC_H
#define POSIX_IOCTL_RTC_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* ================================================================
 * RTC (Real-Time Clock) — 片上/外挂实时时钟设备抽象
 *
 * 语义定位：
 *   /dev/rtc<N> 表示一颗独立的 RTC 硬件（片上 RTC / 外挂 PCF8563 / …）。
 *   read/write 不承载业务，全部功能走 ioctl。
 *   若上层需要"系统 wall clock"（time_t / gettimeofday 语义），
 *   请在应用层基于本设备再包一层软时钟；本层只暴露硬件能力。
 *
 * 时间表示：
 *   本层结构体采用 "自然值" 语义（year = 完整年份，month = 1..12），
 *   与 <time.h> 的 struct tm（year - 1900, month 0..11）不同，
 *   目的是让驱动/上层看到最不易出错的形式；具体端口负责与硬件寄存器
 *   或 Zephyr struct rtc_time 之间做转换。
 *
 * 闹钟（alarm）：
 *   闹钟以 (id, mask, time) 三元组配置。mask 指示哪些字段参与比较，
 *   例如 mask = MONTHDAY|HOUR|MINUTE 表示"每月 X 号 HH:MM 触发"；
 *   mask = 0 表示禁用该 id。回调可能在中断/工作队列上下文触发，取决
 *   于端口实现——上层回调应做到快速、非阻塞、不再持有本设备 fd。
 *
 * 端口能力探测：
 *   不同芯片支持的字段/闹钟数/校准范围不同。
 *   POSIX_RTC_IOCTL_GET_CAPS 返回本 RTC 的能力清单，上层据此裁剪逻辑。
 * ================================================================ */

/* -------- alarm 字段掩码（与 Zephyr RTC_ALARM_TIME_MASK_* 对齐） -------- */
#define POSIX_RTC_ALARM_MASK_SECOND    (1u << 0)
#define POSIX_RTC_ALARM_MASK_MINUTE    (1u << 1)
#define POSIX_RTC_ALARM_MASK_HOUR      (1u << 2)
#define POSIX_RTC_ALARM_MASK_MONTHDAY  (1u << 3)
#define POSIX_RTC_ALARM_MASK_MONTH     (1u << 4)
#define POSIX_RTC_ALARM_MASK_YEAR      (1u << 5)
#define POSIX_RTC_ALARM_MASK_WEEKDAY   (1u << 6)
#define POSIX_RTC_ALARM_MASK_YEARDAY   (1u << 7)
#define POSIX_RTC_ALARM_MASK_NSEC      (1u << 8)

/* -------- 时间结构 --------
 * year   : 完整年份 (例如 2026)
 * month  : 1..12
 * mday   : 1..31
 * hour   : 0..23
 * minute : 0..59
 * second : 0..60  (60 保留给闰秒；一般驱动只用到 59)
 * wday   : 0..6   (0 = Sunday；驱动填不了时置 0xFF)
 * yday   : 0..365 (驱动填不了时置 0xFFFF)
 * nsec   : 0..999_999_999
 */
typedef struct
{
    uint16_t year;
    uint8_t  month;
    uint8_t  mday;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  wday;
    uint16_t yday;
    uint32_t nsec;
} posix_rtc_time_t;

/* -------- alarm 配置 -------- */
typedef struct
{
    uint16_t         id;         /* 闹钟通道号，0..caps.alarm_count-1 */
    uint16_t         mask;       /* POSIX_RTC_ALARM_MASK_*，0 表示禁用 */
    posix_rtc_time_t time;       /* 触发条件；mask=0 时忽略 */
    /* 回调可能在 ISR / 工作队列上下文触发，视端口而定。 */
    void (*callback)(posix_fd_t fd, uint16_t id, void *arg);
    void  *arg;
} posix_rtc_alarm_t;

/* -------- 秒进位（update）回调 --------
 * 用于秒中断计数、软 wall clock 校准等场景。
 * 回调上下文与 alarm 相同（端口相关）。
 */
typedef struct
{
    void (*callback)(posix_fd_t fd, void *arg);
    void  *arg;
} posix_rtc_update_t;

/* -------- 能力描述 --------
 * alarm_count       : 硬件闹钟通道数量，0 表示不支持闹钟
 * alarm_mask        : 每通道支持的字段掩码 (POSIX_RTC_ALARM_MASK_*)
 * has_update_irq    : 是否支持秒进位回调
 * has_calibration   : 是否支持晶振校准
 */
typedef struct
{
    uint16_t alarm_count;
    uint16_t alarm_mask;
    uint8_t  has_update_irq;
    uint8_t  has_calibration;
    uint8_t  _rsv[2];
} posix_rtc_caps_t;

/* -------- ioctl 命令 -------- */
#define POSIX_RTC_IOCTL_GET_TIME         POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 1)
#define POSIX_RTC_IOCTL_SET_TIME         POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 2)
#define POSIX_RTC_IOCTL_GET_CAPS         POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 3)

/* 闹钟：SET_ALARM 用 posix_rtc_alarm_t（含 callback），
 * GET_ALARM 返回当前配置（callback 字段不填），
 * IS_PENDING arg = uint32_t*，通道 id 编码在 cmd 低位无关处，改由 arg->id 传入。
 * 为简化，我们把 id 编在 arg 结构里，命令本身不带 id。 */
#define POSIX_RTC_IOCTL_SET_ALARM        POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 4)
#define POSIX_RTC_IOCTL_GET_ALARM        POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 5)

/* IS_PENDING: arg = posix_rtc_alarm_pending_t*，驱动读走并清除 pending 标志 */
typedef struct
{
    uint16_t id;
    uint8_t  pending;   /* out: 0/1 */
    uint8_t  _rsv;
} posix_rtc_alarm_pending_t;
#define POSIX_RTC_IOCTL_IS_ALARM_PENDING POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 6)

/* 秒进位回调：arg = posix_rtc_update_t*，callback = NULL 表示注销 */
#define POSIX_RTC_IOCTL_SET_UPDATE_CB    POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 7)

/* 校准：arg = int32_t*，单位由端口决定（一般 ppb / 半 ppm）。
 * 若硬件不支持返回 POSIX_ERR_NOSUPP。 */
#define POSIX_RTC_IOCTL_SET_CALIBRATION  POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 8)
#define POSIX_RTC_IOCTL_GET_CALIBRATION  POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 9)

#endif /* POSIX_IOCTL_RTC_H */
