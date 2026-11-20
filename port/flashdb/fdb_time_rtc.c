/* ================================================================
 * FlashDB TSDB time source -- wall-clock seconds (1970 epoch)
 *
 * Reads the current wall-clock through the posix RTC device (/dev/rtc0), converts it to Unix epoch seconds
 * and returns it to FlashDB TSDB as the timestamp. A business-side tsdb just passes this
 * function pointer to fdb_tsdb_init (see flashdb_registry.c). The device does no timezone conversion.
 *
 * Design trade-offs:
 *   - Depends only on the posix RTC abstraction -- does not include <time.h> or zephyr.
 *     Calendar conversion uses Hinnant's closed-form days_from_civil (10 lines vs. mktime,
 *     which would also need TZ/newlib handling); more transparent, fewer dependencies.
 *   - The RTC calendar fields are exactly the device display time and are converted to a scalar by
 *     UTC calendar rules; "UTC rules" here only means a timezone-free calendar algorithm, not that the data stream carries UTC time.
 *   - Monotonic compensation lives here: at second resolution, multiple appends within one second hit the same ts,
 *     and FDB internally rejects cur <= last; here we do last+1 so sequential appends succeed at the cost that
 *     timestamps "inflate" into the future under high write rates (business side should limit write frequency).
 *   - On RTC read failure, return s_last_time so FDB reliably rejects -- on first power-on the RTC
 *     has already been backed by rtc_init with build time, so this path is not normally reached.
 *   - fdb_cfg.h enables FDB_USING_TIMESTAMP_64BIT, avoiding uint32_t wall-clock seconds
 *     being interpreted as negative after 2038 by the default signed 32-bit fdb_time_t.
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_rtc.h"
#include <flashdb.h>

/* /dev/rtc0 lazy-open; held until process exit. */
static posix_fd_t s_rtc_fd    = POSIX_FD_NULL;
static fdb_time_t s_last_time = 0;

/* posix_rtc_time_t → wall clock seconds (1970 epoch).
 * Uses Howard Hinnant's days_from_civil algorithm: March is treated as the first month of the year, so leap days
 * fall naturally at year end and the formula becomes purely linear. Valid for year >= 1970. */
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
            return s_last_time;   /* make fdb_tsl_append reject due to cur <= last */
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
