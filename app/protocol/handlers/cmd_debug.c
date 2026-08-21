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
 * frame.  That subcmd now exists -- 0x05 WIFI_DATA_TX sends its bytes over
 * Wi-Fi.  Every other subcmd still ignores the value and only logs it.
 *
 * Every path answers 0x04 RESULT.  A debug command that silently did nothing
 * on a malformed request would be indistinguishable from a dead BLE link,
 * which is precisely what this command exists to rule out.
 *
 * THE WI-FI SUBCMDS ARE ASYNCHRONOUS AND THEIR RESULT IS NOT THE ANSWER.
 * The 8711 is the SPI master: it owns SCLK/CS and polls roughly every 2 s while
 * idle, so this side can only stage a slot and wait to be clocked.  A reply
 * therefore needs one poll out and one poll back, arriving 2..4 s later on the
 * transport thread -- long after this handler has returned.  Blocking here to
 * wait for it would stall the l2 task (the single serialiser for every BLE
 * command) for seconds, so we do not: SUCCEED means "the query was queued", and
 * the reply is printed to the device log by wifi_8711_at_query.c.
 *
 * The two data-tunnel subcmds (0x04 / 0x05) are reserved and will NOT work
 * against current 8711 firmware -- its AT parser knows only WLSTATE and
 * WLSTARTAP.  They stage successfully and the rejection appears in the log a
 * few seconds later; after that the tunnel latches off and both answer
 * NOT_READY without touching the wire.  See wifi_8711/wifi_8711_at_data.h.
 *
 * An App that wants the AP credentials as DATA should send 0x12 GET_AP_INFO,
 * which answers 0x13 from a warm cache; these subcmds are for a human watching
 * the console during bring-up.
 */
#include <stdint.h>
#include <errno.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"
#if defined(CONFIG_WIFI_8711)
#include "wifi_8711_at_query.h"
#include "wifi_8711_at_data.h"
#endif

/**
 * @brief  Map an errno-style return from the Wi-Fi layer onto an EB_RESULT_*.
 *
 * -ENODEV means the 8711 link never initialised, which is a "not ready", not a
 * failure -- the App can retry.  -EBUSY means the single-flight AT layer is
 * occupied, which is also transient.  Everything else is a real failure.
 */
#if defined(CONFIG_WIFI_8711)
static uint8_t wifi_rc_to_result(int rc)
{
    if (rc == 0)
    {
        return EB_RESULT_SUCCEED;
    }
    if (rc == -ENODEV)
    {
        return EB_RESULT_NOT_READY;
    }
    if (rc == -ENOTSUP)
    {
        /* The reserved data tunnel, latched off after the 8711 answered
         * "[AT]:ERROR".  NOT_READY rather than FAILED: nothing is broken, the
         * peer firmware simply does not implement the command yet, and retrying
         * is pointless until it does.  See wifi_8711_at_data.h. */
        return EB_RESULT_NOT_READY;
    }
    /* -EBUSY: the AT layer is single flight and something else has it -- most
     * likely port_softap's background AP poll.  BUSY, not FAILED: retrying in a
     * few seconds will work, and telling the App it failed would send it
     * looking for a fault that is not there. */
    return (rc == -EBUSY) ? EB_RESULT_BUSY : EB_RESULT_FAILED;
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

    case EB_DBG_SUB_WIFI_DATA_RX:
#if defined(CONFIG_WIFI_8711)
        /* Poll the reserved inbound tunnel.  The message, if any, is printed by
         * wifi_8711_at_data.c -- getting it back over BLE would need a new
         * response command, and the tunnel has no firmware behind it yet. */
        EBADGE_LOG("DEBUG: WIFI_DATA_RX -> AT+WLRECV (reply goes to the log)");
        return wifi_rc_to_result(wifi_8711_at_data_poll(NULL, NULL));
#else
        EBADGE_WARN("DEBUG: WIFI_DATA_RX but CONFIG_WIFI_8711=n");
        (void)val;
        return EB_RESULT_NOT_READY;
#endif

    case EB_DBG_SUB_WIFI_DATA_TX:
#if defined(CONFIG_WIFI_8711)
        /* The one subcmd that uses the VALUE TLV.  Refuse an absent or empty
         * one rather than sending a zero-length message: "send nothing" is a
         * malformed request, and answering SUCCEED to it would suggest bytes
         * went out. */
        if (val == NULL || val->len == 0U)
        {
            EBADGE_WARN("DEBUG: WIFI_DATA_TX needs a non-empty VALUE TLV");
            return EB_RESULT_FAILED;
        }
        EBADGE_LOG1("DEBUG: WIFI_DATA_TX -> AT+WLSEND %d B", (int)val->len);
        return wifi_rc_to_result(wifi_8711_at_data_send(val->val, val->len,
                                                        NULL, NULL));
#else
        EBADGE_WARN("DEBUG: WIFI_DATA_TX but CONFIG_WIFI_8711=n");
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
