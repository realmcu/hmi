#ifndef POSIX_IOCTL_RTC_H
#define POSIX_IOCTL_RTC_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* ================================================================
 * RTC (Real-Time Clock) — on-chip/external real-time clock device abstraction
 *
 * Semantics:
 *   /dev/rtc<N> represents an independent RTC device (on-chip RTC / external PCF8563 / ...).
 *   read/write carry no business logic; all functionality goes through ioctl.
 *   If the upper layer needs a "system wall clock" (time_t / gettimeofday semantics),
 *   wrap a software clock in the application on top of this device; this layer exposes only hardware capabilities.
 *
 * Time representation:
 *   Structs here use "natural value" semantics (year = full year, month = 1..12),
 *   different from <time.h>'s struct tm (year - 1900, month 0..11);
 *   the goal is the least error-prone form for drivers/upper layers; the port handles conversion
 *   to/from hardware registers or Zephyr struct rtc_time.
 *
 * Alarm:
 *   Alarms are configured as an (id, mask, time) triple. mask indicates which fields take part in comparison,
 *   e.g. mask = MONTHDAY|HOUR|MINUTE means "trigger on day X of each month at HH:MM";
 *   mask = 0 disables that id. The callback may fire in interrupt/workqueue context, depending
 *   on the port implementation -- upper-layer callbacks should be fast, non-blocking, and not hold this device's fd.
 *
 * Port capability discovery:
 *   Different chips support different fields/alarm counts/calibration ranges.
 *   POSIX_RTC_IOCTL_GET_CAPS returns this RTC's capability list; the upper layer trims its logic accordingly.
 * ================================================================ */

/* -------- alarm field masks (aligned with Zephyr RTC_ALARM_TIME_MASK_*) -------- */
#define POSIX_RTC_ALARM_MASK_SECOND    (1u << 0)
#define POSIX_RTC_ALARM_MASK_MINUTE    (1u << 1)
#define POSIX_RTC_ALARM_MASK_HOUR      (1u << 2)
#define POSIX_RTC_ALARM_MASK_MONTHDAY  (1u << 3)
#define POSIX_RTC_ALARM_MASK_MONTH     (1u << 4)
#define POSIX_RTC_ALARM_MASK_YEAR      (1u << 5)
#define POSIX_RTC_ALARM_MASK_WEEKDAY   (1u << 6)
#define POSIX_RTC_ALARM_MASK_YEARDAY   (1u << 7)
#define POSIX_RTC_ALARM_MASK_NSEC      (1u << 8)

/* -------- Time structure --------
 * year   : full year (e.g. 2026)
 * month  : 1..12
 * mday   : 1..31
 * hour   : 0..23
 * minute : 0..59
 * second : 0..60  (60 reserved for leap seconds; most drivers only use 59)
 * wday   : 0..6   (0 = Sunday; set to 0xFF when the driver cannot fill it)
 * yday   : 0..365 (set to 0xFFFF when the driver cannot fill it)
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

/* -------- Alarm config -------- */
typedef struct
{
    uint16_t         id;         /* alarm channel number, 0..caps.alarm_count-1 */
    uint16_t         mask;       /* POSIX_RTC_ALARM_MASK_*, 0 means disabled */
    posix_rtc_time_t time;       /* trigger condition; ignored when mask=0 */
    /* callback may fire in ISR / workqueue context, depending on the port. */
    void (*callback)(posix_fd_t fd, uint16_t id, void *arg);
    void  *arg;
} posix_rtc_alarm_t;

/* -------- Second tick (update) callback --------
 * For second-interrupt counting, software wall clock calibration, etc.
 * Callback context is the same as for alarm (port-dependent).
 */
typedef struct
{
    void (*callback)(posix_fd_t fd, void *arg);
    void  *arg;
} posix_rtc_update_t;

/* -------- Capability description --------
 * alarm_count       : number of hardware alarm channels, 0 means alarms unsupported
 * alarm_mask        : field mask supported per channel (POSIX_RTC_ALARM_MASK_*)
 * has_update_irq    : whether second tick callback is supported
 * has_calibration   : whether crystal calibration is supported
 */
typedef struct
{
    uint16_t alarm_count;
    uint16_t alarm_mask;
    uint8_t  has_update_irq;
    uint8_t  has_calibration;
    uint8_t  _rsv[2];
} posix_rtc_caps_t;

/* -------- ioctl commands -------- */
#define POSIX_RTC_IOCTL_GET_CALENDAR     POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 1)
#define POSIX_RTC_IOCTL_SET_CALENDAR     POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 2)
#define POSIX_RTC_IOCTL_GET_CAPS         POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 3)

/* Alarm: SET_ALARM uses posix_rtc_alarm_t (with callback),
 * GET_ALARM returns the current config (callback field not filled),
 * IS_PENDING arg = uint32_t*; the channel id is now passed via arg->id, not encoded in the cmd low bits.
 * For simplicity we encode the id in the arg struct; the command itself carries no id. */
#define POSIX_RTC_IOCTL_SET_ALARM        POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 4)
#define POSIX_RTC_IOCTL_GET_ALARM        POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 5)

/* IS_PENDING: arg = posix_rtc_alarm_pending_t*, the driver reads and clears the pending flag */
typedef struct
{
    uint16_t id;
    uint8_t  pending;   /* out: 0/1 */
    uint8_t  _rsv;
} posix_rtc_alarm_pending_t;
#define POSIX_RTC_IOCTL_IS_ALARM_PENDING POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 6)

/* Second tick callback: arg = posix_rtc_update_t*, callback = NULL means unregister */
#define POSIX_RTC_IOCTL_SET_UPDATE_CB    POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 7)

/* Calibration: arg = int32_t*, unit is port-defined (usually ppb / half ppm).
 * Returns POSIX_ERR_NOSUPP if the hardware does not support it. */
#define POSIX_RTC_IOCTL_SET_CALIBRATION  POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 8)
#define POSIX_RTC_IOCTL_GET_CALIBRATION  POSIX_IOC(POSIX_DEVICE_MAGIC_RTC, 9)

#endif /* POSIX_IOCTL_RTC_H */
