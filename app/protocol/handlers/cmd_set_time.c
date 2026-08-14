/**
 * @file    cmd_set_time.c
 * @brief   0x01 SET_TIME -- App pushes a broken-down date/time.  App-layer TODO.
 *
 * Spec §4.1 carries ONE TLV (type 0x01) whose value is the 8-byte
 * vs_date_time struct -- not a Unix timestamp.  Layout (see ebadge_cmd.h):
 *
 *   [0:2] year u16 LE   [2] month  [3] day
 *   [4] hours  [5] minutes  [6] seconds  [7] day-of-week (1=Mon..7=Sun)
 */
#include <stdint.h>
#include <stdbool.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"

/** Range-check per spec §4.1 so the app never sees a nonsense calendar. */
static bool dtime_valid(uint16_t year, uint8_t mon, uint8_t day,
                        uint8_t hh, uint8_t mm, uint8_t ss, uint8_t dow)
{
    return (year >= 1582 && year <= 9999)
           && (mon  >= 1 && mon  <= 12)
           && (day  >= 1 && day  <= 31)
           && (hh   <= 23)
           && (mm   <= 59)
           && (ss   <= 59)
           && (dow  >= 1 && dow  <= 7);
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
    EBADGE_LOG3("SET_TIME: %02d:%02d:%02d  (app-layer RTC hook TODO)",
                (int)hh, (int)mm, (int)ss);

    /* TODO(app): forward to the RTC subsystem.  The values above are already
     * broken down exactly the way /dev/rtc0 wants them, so the wire-up is a
     * direct field copy -- no epoch conversion needed.  Post it across with
     * ebadge_task_post_call() and a heap-owned copy, since `tlvs` only lives
     * for this call.  See [[ebadge8773g-rtc-enable-posix]].
     */

    (void)ebadge_l2_result_send(EB_CMD_SET_TIME, EB_RESULT_SUCCEED);
}
