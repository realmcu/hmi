/**
 * @file    xfer_notify.h
 * @brief   Notify builders shared by BOTH Wi-Fi data-plane sessions.
 *
 * 0x13 AP_INFO is not a per-session command at all: the file transfer (0x10, §5),
 * the stream preview (0x08, §6) and the 8711 firmware update (0xE0) raise the same
 * SoftAP on the same port, so all three send byte-identical credentials.
 *
 * PROGRESS, FAIL and DECISION are a different kind of sharing.  The update has its
 * own command numbers for them (0xE4 / 0xE6 / 0xE1) but the SAME TLVs as the 0x1n
 * twins, so what is shared is the payload builder and not the command byte -- hence
 * the _on() variants at the bottom of this file.
 *
 * Either way the criterion is the number of senders, not the complexity of the
 * builder: one builder per payload shape means the TLV order can only be got wrong
 * in one place.  0x15 XFER_DONE stays in xfer_session.c because its 0xE5 twin does
 * NOT share its TLVs.
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

/*----------------------------------------------------------------------------*
 *  Same builders, explicit command byte
 *
 *  The 8711 update sends these three payloads under its own command numbers
 *  (0xE4 / 0xE6 / 0xE1) and with the SAME TLVs the 0x1n twins use -- the offset
 *  rule is in the 0xE0 block of ebadge_cmd.h.  So the command is a parameter
 *  rather than a constant, and the two wire commands share one builder instead of
 *  two copies that have to be kept in step by hand.
 *
 *  The two functions above are thin wrappers that pin the file-transfer numbers,
 *  which is why every existing call site is unchanged.
 *
 *  There is no eb_emit_done_on(): 0x15 and 0xE5 do NOT agree on their TLVs (an
 *  update has no file id), so sharing a builder there would mean a parameter for
 *  every field and no shared logic left.
 *----------------------------------------------------------------------------*/
void eb_emit_progress_on(uint8_t cmd, uint32_t recv, uint32_t total);
void eb_emit_fail_on(uint8_t cmd, uint8_t reason, const char *detail);

/**
 * @brief  Emit a DECISION notify -- 0x11 (spec §4.8) or 0xE1.
 * @param  cmd       EB_CMD_XFER_DECISION or EB_CMD_OTA_DECISION.
 * @param  decision  required, EB_DECISION_*.
 * @param  reason    optional, EB_XFER_ERR_*; pass 0 to omit the TLV.
 *
 * xfer_session.c keeps its own static copy of this for 0x11 and is not obliged to
 * move to it; the OTA session uses this one.  Both write the same two TLVs.
 */
void eb_emit_decision_on(uint8_t cmd, uint8_t decision, uint8_t reason);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_XFER_NOTIFY_H_ */
