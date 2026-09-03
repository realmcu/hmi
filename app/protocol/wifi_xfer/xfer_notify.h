/**
 * @file    xfer_notify.h
 * @brief   Notify builders shared by BOTH Wi-Fi data-plane sessions.
 *
 * 0x13 AP_INFO, 0x14 XFER_PROGRESS and 0x16 XFER_FAIL are not per-session
 * commands: the file transfer (0x10, §5), the stream preview (0x08, §6) and the
 * 8711 firmware update (0xE0) raise the same SoftAP, count bytes into the same
 * kind of receive cache, and report failure with the same §2.6 reason codes.
 * Keeping one builder per wire command means the TLV order can only be got wrong
 * in one place.
 *
 * Session-specific notifies (0x11, 0x09, 0x15) stay in their own session .c --
 * they are not shared and do not belong here.  0x14 was one of those until the
 * OTA session needed it; the criterion is the number of senders, not the
 * complexity of the builder.
 *
 * Call on l2_task context only, like every other notify emitter.
 */
#ifndef _EBADGE_XFER_NOTIFY_H_
#define _EBADGE_XFER_NOTIFY_H_

#include <stdint.h>
#include "../port/ebadge_port_softap.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Emit 0x13 AP_INFO (spec §4.9) -- all seven TLVs, in spec order.
 * @param  info      creds as REPORTED BY the radio, not chosen by us; a
 *                   password of "" is emitted as security=Open.
 * @param  tcp_port  the port the AP's own server is listening on.
 */
void eb_emit_ap_info(const ebadge_softap_info_t *info, uint16_t tcp_port);

/**
 * @brief  Emit 0x14 XFER_PROGRESS (spec §4.10) -- ABSOLUTE byte counters.
 *
 * Not a percentage: the spec carries recv/total and lets the App do the division,
 * which is also what lets a receiver report progress before it knows the total.
 * Throttling is the CALLER's job (§4.10 asks for 200 ms or each 5% boundary,
 * whichever comes first) because the trigger depends on what the caller is
 * counting -- this builder has no memory between calls.
 */
void eb_emit_progress(uint32_t recv, uint32_t total);

/**
 * @brief  Emit 0x16 XFER_FAIL (spec §4.12).
 * @param  reason  required, EB_XFER_ERR_*.
 * @param  detail  optional debug string; NULL or "" omits the TLV.
 */
void eb_emit_fail(uint8_t reason, const char *detail);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_XFER_NOTIFY_H_ */
