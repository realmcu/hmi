/**
 * @file    cmd_debug.c
 * @brief   0xFF DEBUG -- vendor bring-up hook, answered by 0x04 RESULT.
 *
 * Not part of PROT-001.  0xFF sits outside every allocated and reserved range
 * in §3, so it cannot collide with a future spec revision.
 *
 * TLVs (see the 0xFF block in ebadge_cmd.h):
 *   0x01 subcmd  1B, required, EB_DBG_SUB_*
 *   0x02 value   optional, reserved -- accepted and logged, not interpreted
 *
 * The value TLV exists so the wire format is already final: the first subcmd
 * that needs an argument can carry one without the App having to relearn the
 * frame.  Until then it is dumped to the log and otherwise ignored.
 *
 * Every path answers 0x04 RESULT.  A debug command that silently did nothing
 * on a malformed request would be indistinguishable from a dead BLE link,
 * which is precisely what this command exists to rule out.
 *
 * THE WI-FI SUBCMDS ARE ASYNCHRONOUS AND THEIR RESULT IS NOT THE ANSWER.
 * The 8711 is the SPI master: it owns SCLK/CS and polls roughly every 2 s, so
 * this side can only stage a slot and wait to be clocked.  A reply therefore
 * needs one poll out and one poll back, arriving 2..4 s later on the transport
 * thread -- long after this handler has returned.  Blocking here to wait for it
 * would stall the l2 task (the single serialiser for every BLE command) for
 * seconds, so we do not: SUCCEED means "the query was queued", and the reply
 * is printed to the device log by the sink in wifi_8711_at_query.c.
 */
#include <stdint.h>
#include <errno.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"
#if defined(CONFIG_WIFI_8711)
#include "wifi_8711_at_query.h"
#endif

/**
 * @brief  Map an errno-style return from the Wi-Fi layer onto an EB_RESULT_*.
 *
 * -ENODEV means the 8711 link never initialised, which is a "not ready", not a
 * failure -- the App can retry.  Everything else is a real failure.
 */
#if defined(CONFIG_WIFI_8711)
static uint8_t wifi_rc_to_result(int rc)
{
    if (rc == 0)
    {
        return EB_RESULT_SUCCEED;
    }
    return (rc == -ENODEV) ? EB_RESULT_NOT_READY : EB_RESULT_FAILED;
}
#endif

/** Handle one debug sub-function.  @return an EB_RESULT_* code. */
static uint8_t debug_dispatch_sub(uint8_t sub, const ebadge_tlv_t *val)
{
    switch (sub)
    {
    case EB_DBG_SUB_PING:
        /* Liveness only: reaching here proves BLE link + CCCD + frame
         * reassembly + TLV parse + dispatch table are all working.       */
        EBADGE_LOG("DEBUG: PING");
        return EB_RESULT_SUCCEED;

    case EB_DBG_SUB_WIFI_AP_INFO:
#if defined(CONFIG_WIFI_8711)
        /* AT+WLSTATE -- the 8711 answers with SSID / PASSWD / IP / PORT /
         * CLIENTS as plain text.  Staged only; see the file header. */
        EBADGE_LOG("DEBUG: WIFI_AP_INFO -> AT+WLSTATE (reply goes to the log)");
        return wifi_rc_to_result(wifi_8711_at_query_ap_info());
#else
        /* Built without the wifi_8711 snippet: there is no SPI link at all.
         * Say so rather than reporting a generic failure, because the fix is
         * a build flag, not anything on the wire. */
        EBADGE_WARN("DEBUG: WIFI_AP_INFO but CONFIG_WIFI_8711=n");
        (void)val;
        return EB_RESULT_NOT_READY;
#endif

    case EB_DBG_SUB_WIFI_START_AP:
#if defined(CONFIG_WIFI_8711)
        EBADGE_LOG("DEBUG: WIFI_START_AP -> AT+WLSTARTAP (reply goes to the log)");
        return wifi_rc_to_result(wifi_8711_at_start_ap());
#else
        EBADGE_WARN("DEBUG: WIFI_START_AP but CONFIG_WIFI_8711=n");
        (void)val;
        return EB_RESULT_NOT_READY;
#endif

    default:
        /* Includes EB_DBG_SUB_NONE (0x00).  Unknown subcmds are rejected
         * rather than ignored so a typo'd id is visible on the App side. */
        EBADGE_WARN1("DEBUG: unknown subcmd 0x%02x", sub);
        (void)val;
        return EB_RESULT_FAILED;
    }
}

void handle_debug(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    uint8_t sub = 0;
    if (!ebadge_tlv_get_u8(tlvs, n_tlv, EB_TLV_DBG_SUBCMD, &sub))
    {
        /* get_u8 fails on both "absent" and "wrong length", and the two are
         * worth telling apart while bringing the App up.                    */
        const ebadge_tlv_t *t = ebadge_tlv_find(tlvs, n_tlv, EB_TLV_DBG_SUBCMD);
        if (t == NULL)
        {
            EBADGE_WARN("DEBUG: no SUBCMD TLV -> RESULT FAILED");
        }
        else
        {
            EBADGE_WARN2("DEBUG: SUBCMD len=%d, want %d -> RESULT FAILED",
                         (int)t->len, (int)EB_DBG_SUBCMD_LEN);
        }
        (void)ebadge_l2_result_send(EB_CMD_DEBUG, EB_RESULT_FAILED);
        return;
    }

    /* Reserved argument -- log it, do not act on it. */
    const ebadge_tlv_t *val = ebadge_tlv_find(tlvs, n_tlv, EB_TLV_DBG_VALUE);
    if (val != NULL)
    {
        if (val->len > EB_DBG_VALUE_MAX)
        {
            EBADGE_WARN2("DEBUG: VALUE len=%d > cap %d -> RESULT FAILED",
                         (int)val->len, (int)EB_DBG_VALUE_MAX);
            (void)ebadge_l2_result_send(EB_CMD_DEBUG, EB_RESULT_FAILED);
            return;
        }
        if (val->len)
        {
            EBADGE_LOG_HEX("DEBUG value (reserved)", val->val, val->len);
        }
    }

    EBADGE_LOG2("DEBUG: subcmd=0x%02x vlen=%d", sub,
                (int)(val ? val->len : 0));

    uint8_t code = debug_dispatch_sub(sub, val);
    (void)ebadge_l2_result_send(EB_CMD_DEBUG, code);
}
