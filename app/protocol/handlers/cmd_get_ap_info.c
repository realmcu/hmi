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
 * WHY THIS ANSWERS FROM A CACHE
 * ---------------------------------------------------------------------------
 * The credentials live in the 8711, and asking it costs 2..4 s on an idle SPI
 * link.  This handler runs on l2_task -- the single serialiser for every BLE
 * command -- so waiting here would stall the whole control plane for seconds
 * and, worse, would make a command whose entire job is "tell me quickly" the
 * slowest one in the protocol.
 *
 * So port_softap keeps the values warm in the background and this reads them
 * synchronously.  That is sound because the 8711 has no mechanism to change its
 * own SSID / password / IP / port: a cached answer is not a stale answer, it is
 * the same answer.  (The client count IS volatile, but 0x13 does not carry it.)
 *
 * A cache miss means the link has never answered -- no 8711, or it is not
 * running -- which is NOT_READY, not a failure.
 */
#include <stdint.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"
#include "../port/ebadge_port_softap.h"
#include "../wifi_xfer/xfer_notify.h"

void handle_get_ap_info(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    (void)tlvs; (void)n_tlv;

    ebadge_softap_info_t info;
    uint16_t             port = 0;

    if (!ebadge_port_softap_info(&info, &port))
    {
        EBADGE_LOG("GET_AP_INFO: no AP info known yet -> RESULT NOT_READY");
        (void)ebadge_l2_result_send(EB_CMD_GET_AP_INFO, EB_RESULT_NOT_READY);
        return;
    }

    /* Emitted whether or not a transfer is in progress.  §4.9 frames this as
     * recovery for an App that missed the 0x13, and the credentials are the
     * same either way -- the 8711's AP is up from boot and is not raised per
     * session.  Gating on session state would only mean an App that asked at
     * the wrong moment got NOT_READY for an AP that was in fact ready. */
    EBADGE_LOG3("GET_AP_INFO: -> AP_INFO ssid=\"%s\" port=%u channel=%u",
                info.ssid, (unsigned)port, (unsigned)info.channel);
    eb_emit_ap_info(&info, port);
}
