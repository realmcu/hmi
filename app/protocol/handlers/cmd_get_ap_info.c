/**
 * @file    cmd_get_ap_info.c
 * @brief   0x12 GET_AP_INFO -- the App re-queries the SoftAP info after losing
 *          it (app restart, notify missed).
 *
 * V1.3 §4.9: answer with 0x13 AP_INFO when the AP is ready, and fall back to
 * the generic 0x04 RESULT when it is not.  §2.5 gained VS_CMD_NOT_READY(0x02)
 * for exactly this shape of answer, so the fallback says NOT_READY rather than
 * a flat FAILED -- it tells the App "ask again later" instead of "this command
 * is broken".
 *
 * ---------------------------------------------------------------------------
 * WHY THIS PUTS NOTHING ON THE WIRE
 * ---------------------------------------------------------------------------
 * The credentials live in the 8711, and asking it costs ~10 s on an idle SPI
 * link.  This handler runs on l2_task -- the single serialiser for every BLE
 * command -- so waiting here would stall the whole control plane for seconds
 * and, worse, would make a command whose entire job is "tell me quickly" the
 * slowest one in the protocol.
 *
 * It does not have to ask.  Since protocol v3.1 sec.7.1 the 8711 puts its whole
 * Wi-Fi state in every ~1 Hz POLL payload, so port_softap already holds a copy
 * that is at most about a second old and this reads it synchronously, in
 * microseconds.
 *
 * A FRESH copy, specifically -- ebadge_port_softap_info() refuses to answer from
 * one older than a few seconds, and that gate is the whole reason this is safe.
 * The state MERGES: a zero field means "no news", because an AP=DOWN block
 * reports an empty SSID and the 8711 cannot change its own credentials anyway.
 * So a block from ten seconds ago can name an SSID, password and port that are
 * all still literally correct while there is no radio on the air -- a BLE
 * disconnect switches it off (sec.12.2).  Answering 0x13 from that would send
 * the phone looking for a network that is not there.
 *
 * False therefore means "down, still starting, or the state feed has stopped",
 * which is NOT_READY rather than a failure, and the remedy is the App's: ask
 * again.  A retry a beat later is likely to succeed.  Nothing is provoked from
 * here -- if the feed really has stopped, port_softap's tick is already asking
 * on the same expiry, and letting this path submit too would turn an App
 * retrying 0x12 into an -EBUSY storm against the single-flight AT layer.
 *
 * ---------------------------------------------------------------------------
 * WHICH PORT THIS REPORTS
 * ---------------------------------------------------------------------------
 * The 8711 runs two servers with incompatible admission rules -- 5004 takes bare
 * JPEG, 9000 takes an EBXF header plus body -- so "the port" is not a thing this
 * handler can report.  It has no role of its own either: 0x12 is recovery for a
 * lost 0x13, and the 0x13 it is replacing belonged to a session.
 *
 * So it follows the live session: a running preview gets 5004, anything else
 * gets 9000.  Defaulting to the file port when idle is not arbitrary -- the App
 * sends 0x12 after a restart to resume a transfer, and a preview is cheap to
 * re-offer whereas a half-done file transfer is not.  Either way the phone is
 * told a port that accepts what it is about to send.
 */
#include <stdint.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"
#include "../port/ebadge_port_softap.h"
#include "../wifi_xfer/xfer_notify.h"
#include "../wifi_xfer/stream_session.h"

void handle_get_ap_info(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    (void)tlvs; (void)n_tlv;

    ebadge_softap_info_t  info;
    uint16_t              port = 0;
    ebadge_ap_port_role_t role =
        (stream_session_state() != STREAM_SESSION_IDLE) ? EBADGE_AP_PORT_STREAM
        : EBADGE_AP_PORT_FILE;

    if (!ebadge_port_softap_info(&info, role, &port))
    {
        EBADGE_LOG("GET_AP_INFO: AP not up (or state feed stopped)"
                   " -> RESULT NOT_READY");
        (void)ebadge_l2_result_send(EB_CMD_GET_AP_INFO, EB_RESULT_NOT_READY);
        return;
    }

    if (port == 0U)
    {
        /* Credentials known, port not.  A block can name the AP without naming
         * its servers -- a truncated WLSTATE reply loses the tail, and until the
         * AP is fully up the 8711 reports the ports as 0 (sec.7.4 expresses "no
         * value" as a zero value).  Answering 0x13 with port 0 would send the
         * phone to associate and then connect nowhere.  NOT_READY is the same
         * answer as a total miss because the remedy is the same. */
        EBADGE_LOG1("GET_AP_INFO: %s port not reported yet -> RESULT NOT_READY",
                    (role == EBADGE_AP_PORT_STREAM) ? "stream" : "file");
        (void)ebadge_l2_result_send(EB_CMD_GET_AP_INFO, EB_RESULT_NOT_READY);
        return;
    }

    /* Emitted whether or not a transfer is in progress.  §4.9 frames this as
     * recovery for an App that missed the 0x13, and the credentials do not belong
     * to a session -- the radio's lifetime is the BLE connection (armed on
     * connect, stopped on disconnect), not one transfer.  Gating on session state
     * would only mean an App that asked at the wrong moment got NOT_READY for an
     * AP that was in fact ready. */
    EBADGE_LOG(EB_DIR_FROM_PHONE "0x12 GET_AP_INFO -> answering with the %s port",
               (role == EBADGE_AP_PORT_STREAM) ? "stream" : "file");
    eb_emit_ap_info(&info, port);
}
