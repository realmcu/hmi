/**
 * @file    xfer_notify.h
 * @brief   Notify builders shared by BOTH Wi-Fi data-plane sessions.
 *
 * 0x13 AP_INFO and 0x16 XFER_FAIL are not per-session commands: the file
 * transfer (0x10, §5) and the stream preview (0x08, §6) raise the same SoftAP
 * and report failure with the same §2.6 reason codes.  Keeping one builder
 * per wire command means the TLV order can only be got wrong in one place.
 *
 * Session-specific notifies (0x11, 0x09, 0x14, 0x15) stay in their own
 * session .c -- they are not shared and do not belong here.
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
 * @brief  Emit 0x16 XFER_FAIL (spec §4.12).
 * @param  reason  required, EB_XFER_ERR_*.
 * @param  detail  optional debug string; NULL or "" omits the TLV.
 */
void eb_emit_fail(uint8_t reason, const char *detail);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_XFER_NOTIFY_H_ */
