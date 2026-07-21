/* ================================================================
 * FlashDB TSDB 时间源 —— 覆盖 posix_fdb_ts_get_time 的 weak 默认
 *
 * 默认在 component/posix/examples/port/common/posix_port_fdb_ts.c 里，
 * posix_fdb_ts_get_time 是 k_uptime_get_32()（毫秒单调时钟，重启归零）。
 * 用它做时序数据的 ts 会导致：
 *   1) 重启后 ts 从 0 开始，破坏 tsdb 单调假设
 *   2) 无法按 wall clock 时间范围查询
 *
 * 本文件提供强符号版本，接到 /dev/rtc0，返回 UTC epoch 秒。
 * 32-bit fdb_time_t 到 2038 溢出，产品生命周期内够用。
 *
 * 依赖：/dev/rtc0 已通过 posix_port_init_all() 注册 (posix_port_rtc.c)。
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_rtc.h"

#include <zephyr/sys/timeutil.h>
#include <time.h>
#include "flashdb.h"    /* 引入 fdb_time_t（附带 stdbool.h 等） */

/* /dev/rtc0 只 open 一次并常驻。若 RTC 尚未 ready，重试直到成功。 */
static posix_fd_t s_rtc_fd = POSIX_FD_NULL;

static posix_fd_t rtc_get_fd(void)
{
    if (s_rtc_fd == POSIX_FD_NULL)
    {
        s_rtc_fd = posix_open("/dev/rtc0");
    }
    return s_rtc_fd;
}

fdb_time_t posix_fdb_ts_get_time(void)
{
    posix_fd_t fd = rtc_get_fd();
    if (fd == POSIX_FD_NULL)
    {
        /* fallback：RTC 拿不到，返回 0 让 fdb 用它的 fallback 逻辑
         * （fdb 内部会保证单调递增）。这样 append 不会失败。 */
        return 0;
    }

    posix_rtc_time_t t;
    if (posix_ioctl(fd, POSIX_RTC_IOCTL_GET_TIME, &t) != POSIX_OK)
    {
        return 0;
    }

    struct tm z =
    {
        .tm_year = (int)t.year - 1900,
        .tm_mon  = (int)t.month - 1,
        .tm_mday = t.mday,
        .tm_hour = t.hour,
        .tm_min  = t.minute,
        .tm_sec  = t.second,
        .tm_isdst = -1,
    };
    /* timeutil_timegm: 把 struct tm 视作 UTC，转 unix epoch 秒。
     * 与 mktime 不同——mktime 用本地时区，会随 TZ 变化。 */
    time_t utc = timeutil_timegm(&z);
    return (fdb_time_t)utc;   /* 32-bit ts: 2038-01-19 03:14:07 UTC 溢出 */
}
