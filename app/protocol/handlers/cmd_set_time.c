/**
 * @file    cmd_set_time.c
 * @brief   0x01 SET_TIME -- App pushes a broken-down date/time, we set the RTC.
 *
 * Spec §4.1 carries ONE TLV (type 0x01) whose value is the 8-byte
 * vs_date_time struct -- not a Unix timestamp.  Layout (see ebadge_cmd.h):
 *
 *   [0:2] year u16 LE   [2] month  [3] day
 *   [4] hours  [5] minutes  [6] seconds  [7] day-of-week (1=Mon..7=Sun)
 *
 * ---------------------------------------------------------------------------
 * WHY THE RTC WRITE HAPPENS INLINE
 * ---------------------------------------------------------------------------
 * This handler is dispatched by ebadge_l2 on l2_task, never from an ISR, so it
 * may call the blocking posix API directly -- no ebadge_task_post_call() hop is
 * needed.  That matters twice over: the port refuses ISR callers outright
 * (POSIX_ERR_ISR), and l2_task is the single serialiser for every BLE command,
 * so an RTC register write is affordable here at microseconds but nothing
 * slower should join it.
 *
 * ---------------------------------------------------------------------------
 * THE DAY-OF-WEEK FIELD IS A CHECK, NOT AN INPUT
 * ---------------------------------------------------------------------------
 * Two encodings collide here and neither side can be changed:
 *
 *   spec §4.1 dow       1..7, 1 = Monday, 7 = Sunday
 *   posix_rtc_time_t    0..6, 0 = Sunday   (0xFF = "unknown")
 *
 * We deliberately do NOT convert and forward it, because the hardware would
 * ignore it anyway: rtc_rtl87x3g_set_time() folds the calendar to epoch seconds
 * via timeutil_timegm() and drops tm_wday, while rtc_rtl87x3g_get_time()
 * recomputes the weekday with gmtime_r().  Forwarding the App's value would be
 * write-only code that reads as load-bearing.  0xFF goes down instead, which
 * the port maps to tm_wday = -1 ("unknown"), so nothing downstream mistakes an
 * unset field for a genuine Sunday.
 *
 * The field is still used for what it remains good for: a free consistency
 * check on the App's own calendar.  A mismatch is logged but NOT rejected --
 * the likely cause is a differing dow convention on the App side (0-based, or
 * Sunday-first), and refusing the command over a byte we discard would make
 * clock-setting permanently fail for that App.  The fields that actually reach
 * the hardware are range-checked independently below.
 */
#include <stdint.h>
#include <stdbool.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"

#include "posix.h"
#include "ioctls/posix_ioctl_rtc.h"

static bool is_leap(uint16_t y)
{
    return ((y % 4U) == 0U && (y % 100U) != 0U) || ((y % 400U) == 0U);
}

/** Days in `mon` (1..12) of `year`.  Caller must have range-checked `mon`. */
static uint8_t days_in_month(uint16_t year, uint8_t mon)
{
    static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31
                                   };
    if (mon == 2U && is_leap(year))
    {
        return 29U;
    }
    return dim[mon - 1U];
}

/**
 * @brief  Range-check per spec §4.1 so the clock never takes a nonsense date.
 *
 * The day is checked against the actual month length, not a flat 1..31.  That
 * is not pedantry: timeutil_timegm() in the driver *normalises* rather than
 * rejects, so a "2026-02-31" would be silently stored as 2026-03-03 and the
 * device would then disagree with the phone about the date with nothing in the
 * log to say why.
 *
 * `dow` is range-checked but its agreement with y/m/d is not decided here --
 * see the file header for why that is a warning rather than a rejection.
 */
static bool dtime_valid(uint16_t year, uint8_t mon, uint8_t day,
                        uint8_t hh, uint8_t mm, uint8_t ss, uint8_t dow)
{
    return (year >= 1582 && year <= 9999)
           && (mon  >= 1 && mon  <= 12)
           && (day  >= 1 && day  <= days_in_month(year, mon))
           && (hh   <= 23)
           && (mm   <= 59)
           && (ss   <= 59)
           && (dow  >= 1 && dow  <= 7);
}

/**
 * @brief  Day of week for a Gregorian date, in the spec's own encoding.
 *
 * Sakamoto's method.  Returns 1..7 with 1 = Monday, 7 = Sunday, matching §4.1
 * so the comparison against the App's byte needs no second conversion.
 */
static uint8_t derive_dow(uint16_t year, uint8_t mon, uint8_t day)
{
    static const uint8_t t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    uint16_t y = year;

    if (mon < 3U)
    {
        y--;
    }
    /* Sakamoto yields 0 = Sunday; fold that onto the spec's 7 = Sunday. */
    uint8_t sun0 = (uint8_t)((y + y / 4U - y / 100U + y / 400U
                              + t[mon - 1U] + day) % 7U);
    return (sun0 == 0U) ? 7U : sun0;
}

void handle_set_time(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    const ebadge_tlv_t *t = ebadge_tlv_find(tlvs, n_tlv, EB_TLV_DTIME_VALUE);
    if (!t || t->len != EB_DTIME_LEN)
    {
        EBADGE_WARN1("SET_TIME: bad TLV_DATE_TIME (len=%d, want 8) -> RESULT FAILED",
                     t ? (int)t->len : -1);
        (void)ebadge_l2_result_send(EB_CMD_SET_TIME, EB_RESULT_FAILED);
        return;
    }

    const uint8_t *v    = t->val;
    uint16_t       year = (uint16_t)(v[EB_DTIME_OFF_YEAR] |
                                     ((uint16_t)v[EB_DTIME_OFF_YEAR + 1] << 8));
    uint8_t        mon  = v[EB_DTIME_OFF_MONTH];
    uint8_t        day  = v[EB_DTIME_OFF_DAY];
    uint8_t        hh   = v[EB_DTIME_OFF_HOURS];
    uint8_t        mm   = v[EB_DTIME_OFF_MINUTES];
    uint8_t        ss   = v[EB_DTIME_OFF_SECONDS];
    uint8_t        dow  = v[EB_DTIME_OFF_DOW];

    if (!dtime_valid(year, mon, day, hh, mm, ss, dow))
    {
        EBADGE_WARN2("SET_TIME: out-of-range %04d-%02d... -> RESULT FAILED",
                     (int)year, (int)mon);
        (void)ebadge_l2_result_send(EB_CMD_SET_TIME, EB_RESULT_FAILED);
        return;
    }

    EBADGE_LOG4("SET_TIME: %04d-%02d-%02d dow=%d",
                (int)year, (int)mon, (int)day, (int)dow);
    EBADGE_LOG3("SET_TIME: %02d:%02d:%02d", (int)hh, (int)mm, (int)ss);

    /* Advisory only -- see the file header.  Worth logging because a disagreeing
     * dow is the cheapest evidence available that the App's calendar handling is
     * off, and it costs nothing to surface. */
    uint8_t want_dow = derive_dow(year, mon, day);
    if (want_dow != dow)
    {
        EBADGE_WARN2("SET_TIME: dow=%d disagrees with the date (expect %d) "
                     "-- setting the clock anyway, hardware recomputes it",
                     (int)dow, (int)want_dow);
    }

    posix_rtc_time_t now =
    {
        .year   = year,
        .month  = mon,
        .mday   = day,
        .hour   = hh,
        .minute = mm,
        .second = ss,
        /* 0xFF / 0xFFFF = "unknown"; the port turns them into tm_wday/tm_yday
         * = -1 and the driver recomputes both on read. */
        .wday   = 0xFFU,
        .yday   = 0xFFFFU,
        .nsec   = 0,
    };

    posix_fd_t rtc = posix_open("/dev/rtc0");
    if (rtc == POSIX_FD_NULL)
    {
        /* NOT_READY rather than FAILED, and a RESULT either way: /dev/rtc0 is
         * missing only when the DT node, CONFIG_RTC* or the posix port did not
         * all line up, which is a build-time fault the App cannot fix -- but
         * returning silently would leave the phone waiting for a reply that
         * never comes, which is indistinguishable from a dead link. */
        EBADGE_WARN("SET_TIME: /dev/rtc0 not available -> RESULT NOT_READY");
        (void)ebadge_l2_result_send(EB_CMD_SET_TIME, EB_RESULT_NOT_READY);
        return;
    }

    int rc = posix_ioctl(rtc, POSIX_RTC_IOCTL_SET_TIME, &now);
    (void)posix_close(rtc);

    if (rc != POSIX_OK)
    {
        EBADGE_WARN1("SET_TIME: RTC SET_TIME failed rc=%d -> RESULT FAILED",
                     rc);
        (void)ebadge_l2_result_send(EB_CMD_SET_TIME, EB_RESULT_FAILED);
        return;
    }

    (void)ebadge_l2_result_send(EB_CMD_SET_TIME, EB_RESULT_SUCCEED);
}
