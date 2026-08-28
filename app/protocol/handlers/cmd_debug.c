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
 * frame.  That subcmd now exists -- 0x05 XFER_ACK takes the status and reason
 * bytes it sends to the 8711.  Every other subcmd still ignores the value and
 * only logs it.
 *
 * Every path answers 0x04 RESULT.  A debug command that silently did nothing
 * on a malformed request would be indistinguishable from a dead BLE link,
 * which is precisely what this command exists to rule out.
 *
 * THE WI-FI SUBCMDS ARE ASYNCHRONOUS AND THEIR RESULT IS NOT THE ANSWER.
 * We are the SPI master now, so a command leaves as soon as it is submitted
 * rather than waiting to be clocked -- but the ANSWER still cannot be waited for
 * here.  AT+WLSTATE costs a round trip, and AT+WLSTARTAP does not print OK until
 * the radio is genuinely on the air, which its own handler polls for at 1 Hz for
 * up to 20 iterations.  Blocking on either would stall l2_task -- the single
 * serialiser for every BLE command -- for seconds or tens of seconds.
 *
 * So SUCCEED means "accepted for submission", and the reply is printed to the
 * device log by at_wifi.c when it lands.
 *
 * WHAT THE TRANSFER SUBCMDS TALK TO NOW.  0x04 and 0x05 used to be AT+XFERSTOP /
 * AT+XFERACK -- commands the in-house firmware implemented, which built the EBXR
 * itself out of a status and a reason we handed it.  The standard command set has
 * no such thing: the 8711 is a network card, and the EBXR is eight bytes WE push
 * over the phone's own TCP connection with AT+SKTSENDRAW.
 *
 * They therefore act on ebadge_port_tcp, which is the module that owns those
 * bytes now.  Two consequences for anyone poking them by hand:
 *
 *   - with no phone connected there is no seed link to write to, so both are
 *     no-ops.  Answered NOT_READY rather than FAILED: nothing is broken, there is
 *     simply no connection, which is the normal outcome of poking them idly.
 *   - 0x05 sends a REAL verdict to a REAL phone.  Used mid-upload it ends that
 *     upload with the code typed here.  That is the point of it, and also what is
 *     worth knowing before typing one.
 *
 * An App that wants the AP credentials as DATA should send 0x12 GET_AP_INFO,
 * which answers 0x13 from what we told the 8711 to be; these subcmds are for a
 * human watching the console during bring-up.
 */
#include <stdint.h>
#include <errno.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"
#if defined(CONFIG_WIFI_8711)
#include "at_wifi.h"
#include "../port/ebadge_port_softap.h"
#include "../port/ebadge_port_tcp.h"
#endif

/**
 * @brief  Map an errno-style return from the Wi-Fi / socket layers onto an
 *         EB_RESULT_*.
 *
 * The distinction that matters is transient versus permanent: an App told FAILED
 * goes looking for a fault, and most of what can come back from these layers is
 * "ask again in a moment" instead.
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
        /* The AT layer was never initialised -- no 8711 answered its boot marker,
         * or ebadge_task_init() has not run.  A "not ready", not a failure. */
        return EB_RESULT_NOT_READY;
    }
    if (rc == -ENOTCONN)
    {
        /* No phone has opened a TCP connection, so there is no seed link to act
         * on.  This is what both transfer subcmds return when poked idly, and it
         * is the expected answer rather than a fault. */
        return EB_RESULT_NOT_READY;
    }
    if (rc == -EINVAL || rc == -EMSGSIZE)
    {
        /* The argument was rejected before anything went on the wire.  A real
         * failure, and the caller's to fix. */
        return EB_RESULT_FAILED;
    }
    /* -EBUSY: a parse of the same command is already outstanding (at_wifi keeps
     *         one accumulator, so it refuses a second WLSTATE rather than merging
     *         two answers).
     * -ENOSPC: at_client's submit queue is full -- four commands are already
     *         waiting, which clears in the time the one in flight takes.
     * -EALREADY: an AT+WLSTARTAP is on the wire, and one takes up to ~25 s.
     *
     * All three are BUSY: retrying in a few seconds will work. */
    return ((rc == -EBUSY) || (rc == -ENOSPC) || (rc == -EALREADY))
           ? EB_RESULT_BUSY : EB_RESULT_FAILED;
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
        /* AT+WLSTATE -- the 8711 answers with column-aligned "KEY => VALUE" text
         * meant for a person.  at_wifi parses out the two facts we cannot already
         * know (is it really an AP, how many clients) and logs them; see the file
         * header for why the answer does not come back through here.
         *
         * NULL callback on purpose: this subcmd exists so a human can see the
         * reply, and at_wifi logs it whether anybody asked or not. */
        EBADGE_LOG("DEBUG: WIFI_AP_INFO -> AT+WLSTATE (reply goes to the log)");
        return wifi_rc_to_result(at_wifi_ap_query(NULL, NULL));
#else
        /* Built without the wifi snippet: there is no SPI link at all.  Say so
         * rather than reporting a generic failure, because the fix is a build
         * flag, not anything on the wire. */
        EBADGE_WARN("DEBUG: WIFI_AP_INFO but CONFIG_WIFI_8711=n");
        (void)val;
        return EB_RESULT_NOT_READY;
#endif

    case EB_DBG_SUB_WIFI_START_AP:
#if defined(CONFIG_WIFI_8711)
        /* Through port_softap rather than straight at at_wifi_ap_start(), because
         * the credentials are port_softap's to choose -- it derives them from the
         * BLE address and 0x13 AP_INFO reports what it chose.  Calling at_wifi
         * directly from here would put a DIFFERENT SSID on the air from the one
         * the phone was told to look for.
         *
         * A no-op when the AP is already up or a bring-up is in flight, so this is
         * safe to poke repeatedly; it cannot restart a 25 s command mid-flight. */
        EBADGE_LOG("DEBUG: WIFI_START_AP -> port_softap arm (reply goes to the log)");
        ebadge_port_softap_arm();
        return EB_RESULT_SUCCEED;
#else
        EBADGE_WARN("DEBUG: WIFI_START_AP but CONFIG_WIFI_8711=n");
        (void)val;
        return EB_RESULT_NOT_READY;
#endif

    case EB_DBG_SUB_XFER_STOP:
#if defined(CONFIG_WIFI_8711)
        /* Cut the phone's connection with no EBXR -- the "stop now" half, the same
         * thing xfer_session does when the EBXF header contradicts the BLE offer.
         * On the wire it is AT+SKTDEL on the seed link. */
        if (!ebadge_port_tcp_connected())
        {
            EBADGE_WARN("DEBUG: XFER_STOP but no phone is connected");
            return EB_RESULT_NOT_READY;
        }
        EBADGE_LOG("DEBUG: XFER_STOP -> cutting the TCP connection");
        return wifi_rc_to_result(ebadge_port_tcp_abort());
#else
        EBADGE_WARN("DEBUG: XFER_STOP but CONFIG_WIFI_8711=n");
        (void)val;
        return EB_RESULT_NOT_READY;
#endif

    case EB_DBG_SUB_XFER_ACK:
#if defined(CONFIG_WIFI_8711)
        {
            /* The one subcmd that uses the VALUE TLV: byte 0 status, byte 1 reason.
             * An absent or empty VALUE is refused rather than defaulted -- both 0
             * (fail) and 1 (success) are meaningful, so there is no safe default,
             * and guessing would send the phone a verdict nobody asked for. */
            if (val == NULL || val->len == 0U)
            {
                EBADGE_WARN("DEBUG: XFER_ACK needs VALUE = <status>[,<reason>]");
                return EB_RESULT_FAILED;
            }
            if (!ebadge_port_tcp_connected())
            {
                EBADGE_WARN("DEBUG: XFER_ACK but no phone is connected");
                return EB_RESULT_NOT_READY;
            }

            /* Byte 0 keeps the EBXR's own polarity -- 1 is success, 0 is failure --
             * because that is what a person reading the spec's sec.6.3 table will
             * type.  It is the opposite of the BLE result codes in V1.3 sec.2.5,
             * deliberately, and ebadge_port_tcp_ack() takes a bool so the
             * conversion happens here and only here. */
            bool    ok     = (val->val[0] != 0U);
            uint8_t reason = (val->len >= 2U) ? val->val[1] : 0U;

            EBADGE_LOG2("DEBUG: XFER_ACK -> EBXR status=%u reason=%u",
                        (unsigned)val->val[0], (unsigned)reason);
            /* NULL done: nothing here is sequencing behind the verdict.  The
             * session's own ack() passes a callback because its BLE 0x15/0x16 has
             * to follow the data-plane one; a hand-typed one has no such order. */
            return wifi_rc_to_result(ebadge_port_tcp_ack(ok, reason, NULL, NULL));
        }
#else
        EBADGE_WARN("DEBUG: XFER_ACK but CONFIG_WIFI_8711=n");
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
