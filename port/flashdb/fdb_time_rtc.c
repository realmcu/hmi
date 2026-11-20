/* ================================================================
 * FlashDB TSDB time source —— wall clock seconds (1970 epoch)
 *
 * Reads the current wall clock from the posix RTC device (/dev/rtc0), converts it to Unix
 * epoch seconds and returns it to FlashDB TSDB as the timestamp. Business-side tsdbs simply
 * pass this function pointer to fdb_tsdb_init (see flashdb_registry.c). No timezone conversion.
 *
 * Design trade-offs:
 *   - depends only on the posix RTC abstraction —— no <time.h> include, no zephyr include.
 *     The calendar conversion uses Hinnant's closed-form days_from_civil (10 lines vs. mktime
 *     which also has to handle TZ/newlib), so the code is clearer and has fewer dependencies.
 *   - the RTC calendar fields are the device display time, converted to a scalar by UTC
 *     calendar rules; "UTC" here means only the timezone-free algorithm, not that the data carries UTC time.
 *   - monotonic compensation lives here: at second granularity several appends within one
 *     second hit the same ts, and FDB rejects cur <= last outright, so last+1 lets the in-order
 *     appends succeed; the cost is timestamps "inflating" into the future under heavy write rates.
 *   - on an RTC read failure s_last_time is returned so FDB rejects the append cleanly —— on
 *     first power-on rtc_init already falls back to the build time, so this path is not normal.
 *   - fdb_cfg.h enables FDB_USING_TIMESTAMP_64BIT so uint32_t wall-clock seconds are not
 *     interpreted as negative after 2038 by the default signed 32-bit fdb_time_t.
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_rtc.h"
#include <flashdb.h>

/* /dev/rtc0 lazy-open; held until the process exits. */
static posix_fd_t s_rtc_fd    = POSIX_FD_NULL;
static fdb_time_t s_last_time = 0;

/* posix_rtc_time_t → wall clock seconds (1970 epoch).
 * Uses Howard Hinnant's days_from_civil algorithm: March is treated as the first month of
 * the year, so the leap day falls at year end and the formula becomes purely linear. Valid for year >= 1970. */
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
            return s_last_time;   /* make fdb_tsl_append reject on cur <= last */
        }
    }

    posix_rtc_time_t rt;
    if (posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_GET_CALENDAR, &rt) != POSIX_OK)
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
        now = s_last_time + 1;   /* monotonic compensation for multiple writes within a second */
    }
    s_last_time = now;
    return now;
}
