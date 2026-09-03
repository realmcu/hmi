/**
 * @file    ota_session.h
 * @brief   8711 firmware update session: BLE 0xE0 negotiation, Wi-Fi delivery,
 *          then the image goes over the SPI AT link into the module.
 *
 * Third sibling of xfer_session / stream_session.  It reuses their whole data
 * plane -- the same SoftAP, the same TCP port, the same 0x13/0x14/0x16 notifies --
 * and differs in what happens to the bytes once they arrive:
 *
 *                     xfer (§5)            stream (§6)        ota (0xE0)
 *   opened by         0x10 OFFER           0x08 STREAM_OFFER  0xE0 OTA_OFFER
 *   answered with     0x11 DECISION        0x09 DECISION      0x13 or 0x04
 *   payload framing   40B EBXF + file      N x JPEG frame     none -- raw image
 *   integrity check   size + CRC32         per-frame          NONE (see below)
 *   destination       our own NOR flash    the display        the 8711's flash
 *   ends when         size bytes stored    App closes TCP     module took image
 *
 * All three are mutually exclusive: one radio, one SoftAP, one TCP port.  Each
 * offer path checks the other two and answers BUSY, and this one additionally
 * refuses while a previous push is still in flight -- see ota_session_offer().
 *
 * ---------------------------------------------------------------------------
 * NO INTEGRITY CHECK, AND WHAT THAT ACTUALLY COSTS
 * ---------------------------------------------------------------------------
 * The offer carries a length and nothing else: no version, no CRC, no signature.
 * That is a deliberate first cut, and the honest accounting is:
 *
 *   what still protects us -- the phone's TCP checksums the wire, the SPI AT slots
 *   carry their own CRC between the two chips, and the 8711's own OTA layer
 *   validates the image header before it commits to a bank.  A truncated or
 *   corrupt image is therefore most likely to be REFUSED by the module rather
 *   than installed, and the module keeps running its current firmware.
 *
 *   what does not -- nothing here can tell a wrong-but-valid image from a right
 *   one.  Downgrades, another product's firmware, and a hostile image on the
 *   hotspot are all accepted.  The hotspot is open for the duration of a
 *   transfer, so the trust boundary is "whoever is on the AP", the same as for
 *   file transfer -- but the consequence is a bricked radio rather than a bad
 *   wallpaper.
 *
 * When a check does get added it belongs in the offer TLVs (version + CRC32) and
 * in a verify step between the last received byte and the push, which is why
 * those two points are kept distinct in the state machine below rather than
 * collapsed into one edge.
 *
 * ---------------------------------------------------------------------------
 * WIRE MESSAGES, GIVEN 0xE0 IS THE ONLY NEW COMMAND NUMBER
 * ---------------------------------------------------------------------------
 * Only H->D 0xE0 is new; everything the device says back reuses an existing
 * command, so an App that already speaks file transfer needs no new parsers:
 *
 *   reject   0x04 RESULT(cmd=0xE0, BUSY | FAILED | NOT_READY) and nothing else.
 *   accept   0x13 AP_INFO -- the credentials ARE the acceptance, exactly as they
 *            are for a file transfer once its 0x11 has gone out.
 *   during   0x14 XFER_PROGRESS, throttled, over the RECEIVE phase only.
 *   fail     0x16 XFER_FAIL with a §2.6 reason, once accepted.
 *   success  0x04 RESULT(cmd=0xE0, SUCCEED), after the module has taken the image.
 *
 * So exactly one 0x04 RESULT per session: it carries the refusal when there is
 * one, and otherwise the final verdict.  0x15 XFER_DONE is deliberately not used
 * -- its three required TLVs are a file id, a size and a name, and an OTA has
 * none of those to report truthfully.
 *
 * ---------------------------------------------------------------------------
 * PROGRESS STOPS AT 100% AND THEN NOTHING HAPPENS FOR A WHILE
 * ---------------------------------------------------------------------------
 * Worth knowing before it looks like a hang.  0x14 covers the Wi-Fi receive only.
 * The push that follows is ~64 SPI slots -- fast -- but the 8711 answers the
 * command only after it has written the whole image to its own flash, which is
 * tens of seconds.  The App therefore sees 100%, then a silence, then the final
 * 0x04.  It is not reported as progress because the protocol has no OTA progress
 * notify and inventing a second meaning for 0x14's counters (restarting from
 * zero, or a total of 2x the size) would misinform every existing App.  The push
 * is watchdogged locally instead; see the stall detector in the .c.
 *
 * CONTEXT: every entry point runs on l2_task, like its two siblings.  Callbacks
 * from the transport thread must go through ebadge_task_post_call() first.
 */
#ifndef _EBADGE_OTA_SESSION_H_
#define _EBADGE_OTA_SESSION_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  State
 *
 *  No WAIT_CONFIRM: like the preview stream, the device decides for itself.
 *  No COMPLETING either -- there is no data-plane ack to wait for, because a raw
 *  image has no EBXR verdict to send, so the verdict is a BLE notify and the
 *  session ends on the same edge that emits it.
 *----------------------------------------------------------------------------*/
typedef enum
{
    OTA_SESSION_IDLE = 0,
    OTA_SESSION_WAIT_STA,   /* AP is up, 0x13 is out, waiting for the phone   */
    OTA_SESSION_RECV,       /* buffering the image into PSRAM                 */
    OTA_SESSION_PUSHING,    /* Wi-Fi is down; the image is going over SPI AT  */
} ota_session_state_t;

/*----------------------------------------------------------------------------*
 *  Lifecycle
 *----------------------------------------------------------------------------*/
/** Register with the ebadge_task tick fanout.  Called once at startup. */
void ota_session_init(void);

/** Current state -- the other two sessions test this to detect a busy radio. */
ota_session_state_t ota_session_state(void);

/*----------------------------------------------------------------------------*
 *  Entry points  (all on l2_task)
 *----------------------------------------------------------------------------*/
/**
 * @brief  Handle a decoded 0xE0 OTA_OFFER.  Always answers, on one plane or the
 *         other -- see the wire-message table above.
 *
 * @param  size  image length in bytes, from the offer's size TLV.  This is the
 *               ONLY thing the offer carries that is acted on, and it is load
 *               bearing twice: it is what AT+OTAUSER must declare to the module
 *               before the first byte is pushed, and it is how the receive phase
 *               knows the image is complete -- a raw image has no end marker.
 */
void ota_session_offer(uint32_t size);

/** The phone opened the TCP connection.  Promotes WAIT_STA -> RECV. */
void ota_session_on_sta_joined(void);

/** TCP closed.  Mid-receive this fails the session; during the push it is
 *  expected -- we closed it ourselves before pushing. */
void ota_session_on_tcp_close(int reason /* ebadge_tcp_close_reason_t */);

/**
 * @brief  GAP disconnected, or another session needs the radio -- tear down.
 *
 * Does NOT cancel a push in progress, and that asymmetry with its siblings is
 * intentional: the image is already off the phone and out of the Wi-Fi path, so
 * abandoning it half-written is strictly worse for the module than finishing.
 * The session stops reporting and cleans up when the push completes.
 */
void ota_session_abort(void);

/*----------------------------------------------------------------------------*
 *  Introspection
 *----------------------------------------------------------------------------*/
/** Image bytes received over Wi-Fi so far. */
uint32_t ota_session_bytes_received(void);
/** Image size the offer declared (0 when idle). */
uint32_t ota_session_total_size(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_OTA_SESSION_H_ */
