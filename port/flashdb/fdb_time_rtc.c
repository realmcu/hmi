/* ================================================================
 * FlashDB TSDB 时间源 —— 墙上时钟秒（1970 纪元）
 *
 * 通过 posix RTC 设备 (/dev/rtc0) 读取当前 wall-clock，转 Unix epoch 秒
 * 返回给 FlashDB TSDB 作时间戳。业务侧的 tsdb 通过 fdb_tsdb_init 时把本
 * 函数指针传入即可 (参见 flashdb_registry.c)。设备不做时区换算。
 *
 * 设计取舍：
 *   - 只依赖 posix RTC 抽象 —— 不 include <time.h>、不 include zephyr。
 *     日历换算用 Hinnant days_from_civil 闭式公式（10 行代码 vs. mktime
 *     还得处理 TZ/newlib），代码更透明、依赖更少。
 *   - RTC 日历字段就是设备显示时间，按 UTC 日历规则折算为标量；这里的
 *     UTC 规则仅指无时区的日历算法，不表示数据流中存在 UTC 时间。
 *   - 单调补偿放在这里：秒级精度下，一秒内多次 append 会撞同一个 ts，
 *     FDB 内部 cur <= last 会直接拒收；这里做 last+1 让顺序追加成功，
 *     代价是时间戳在高频写入下会"膨胀"到未来（业务侧应控制写入频率）。
 *   - RTC 读失败时返回 s_last_time 让 FDB 稳稳拒收 —— 首上电场景 RTC
 *     已被 rtc_init 用编译时间兜底，正常不会走到这里。
 *   - fdb_cfg.h 启用 FDB_USING_TIMESTAMP_64BIT，避免 uint32_t 墙上时钟秒
 *     在 2038 年后被默认的有符号 32-bit fdb_time_t 解释为负数。
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_rtc.h"
#include <flashdb.h>

/* /dev/rtc0 lazy-open；持有到进程结束。 */
static posix_fd_t s_rtc_fd    = POSIX_FD_NULL;
static fdb_time_t s_last_time = 0;

/* posix_rtc_time_t → wall clock seconds (1970 epoch).
 * 使用 Howard Hinnant 的 days_from_civil 算法：把三月视为每年首月，闰日
 * 自然落在年末，公式变纯线性。适用于 year >= 1970。 */
static fdb_time_t rtc_time_to_epoch(const posix_rtc_time_t *t)
{
    if (t->year < 1970 || t->month < 1 || t->month > 12 ||
        t->mday  < 1    || t->mday   > 31 ||
        t->hour  > 23   || t->minute > 59 || t->second > 60)
    {
        return (fdb_time_t) - 1;
    }

    int32_t  y   = (int32_t)t->year - (t->month <= 2 ? 1 : 0);
    int32_t  era = y / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);                       /* 0..399 */
    uint32_t m   = t->month + (t->month > 2 ? -3u : 9u);             /* Mar=0..Feb=11 */
    uint32_t doy = (153u * m + 2u) / 5u + t->mday - 1u;              /* 0..365 */
    uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;         /* 0..146096 */
    int64_t  days = (int64_t)era * 146097 + (int64_t)doe - 719468;

    int64_t secs = days * 86400
                   + (int64_t)t->hour   * 3600
                   + (int64_t)t->minute * 60
                   + (int64_t)t->second;
    return (fdb_time_t)secs;
}

fdb_time_t flashdb_get_time(void)
{
    if (s_rtc_fd == POSIX_FD_NULL)
    {
        s_rtc_fd = posix_open("/dev/rtc0");
        if (s_rtc_fd == POSIX_FD_NULL)
        {
            return s_last_time;   /* 让 fdb_tsl_append 因 cur <= last 拒收 */
        }
    }

    posix_rtc_time_t rt;
    if (posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_GET_TIME, &rt) != POSIX_OK)
    {
        return s_last_time;
    }

    fdb_time_t now = rtc_time_to_epoch(&rt);
    if (now == (fdb_time_t) - 1)
    {
        return s_last_time;
    }

    if (now <= s_last_time)
    {
        now = s_last_time + 1;   /* 秒内多次写入的单调补偿 */
    }
    s_last_time = now;
    return now;
}
