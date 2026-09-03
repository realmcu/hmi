/**
 * @file    ota_session.h
 * @brief   8711 firmware update session: BLE 0xE0 negotiation, Wi-Fi delivery,
 *          then the image goes over the SPI AT link into the module.
 *
 * Third sibling of xfer_session / stream_session, and now a deliberately CLOSE
 * copy of the first: same offer/decision handshake, same SoftAP, same TCP port,
 * same 40-byte EBXF header, same whole-payload CRC32, same ack-before-slow-work
 * ordering.  What differs is only where the bytes end up:
 *
 *                     xfer (§5)            stream (§6)        ota (0xE0)
 *   opened by         0x10 OFFER           0x08 STREAM_OFFER  0xE0 OTA_OFFER
 *   accepted with     0x11 DECISION        0x09 DECISION      0xE1 DECISION
 *   credentials       0x13 AP_INFO         0x13 AP_INFO       0x13 AP_INFO
 *   payload framing   40B EBXF + file      N x JPEG frame     40B EBXF + image
 *   integrity check   size + CRC32         per-frame          size + CRC32
 *   progress          0x14                 none               0xE4
 *   destination       our own NOR flash    the display        the 8711's flash
 *   ends when         size bytes stored    App closes TCP     module took image
 *   verdict           0x15 / 0x16          0x16 on abort      0xE5 / 0xE6
 *
 * The 0xEn numbers are the 0x1n numbers plus 0xD0 and carry the same TLVs; the one
 * command NOT twinned is AP_INFO, because the credentials describe the radio rather
 * than what is being sent over it.  See the 0xE0 block in ebadge_cmd.h.
 *
 * All three sessions are mutually exclusive: one radio, one SoftAP, one TCP port.
 * Each offer path checks the other two and refuses with BUSY, and this one
 * additionally refuses while a previous push is still in flight -- see
 * ota_session_offer().
 *
 * ---------------------------------------------------------------------------
 * WHAT THE CRC32 DOES AND DOES NOT PROTECT
 * ---------------------------------------------------------------------------
 * The offer declares size + CRC32 and the EBXF header restates both; they are
 * cross-checked against each other before any payload is taken, and the CRC is
 * accumulated over the received bytes and verified before the first byte is pushed
 * at the module.  So:
 *
 *   caught here -- a truncated image, a corrupt one, a sender that disagrees with
 *   its own offer, and an image offered under the wrong file type.  None of those
 *   now reach the module's flash at all, which is the point of verifying before the
 *   push rather than relying on the module to refuse it afterwards.
 *
 *   NOT caught -- a wrong-but-intact image.  A CRC32 says "these are the bytes the
 *   sender meant to send", not "this is the right firmware for this product".
 *   Downgrades, another product's build, and a hostile image from whoever is on the
 *   hotspot are all accepted, and the consequence is a bricked radio rather than a
 *   bad wallpaper.  Closing that gap needs a signature or a version policy, and the
 *   offer has room for one (0x06 upward is unallocated).  What still helps in the
 *   meantime is that the 8711's own OTA layer validates the image header, so a
 *   plausible-but-foreign image is more likely to be refused than installed.
 *
 * ---------------------------------------------------------------------------
 * PROGRESS STOPS AT 100% AND THEN NOTHING HAPPENS FOR A WHILE
 * ---------------------------------------------------------------------------
 * Worth knowing before it looks like a hang.  0xE4 covers the Wi-Fi receive only.
 * The push that follows is ~64 SPI slots -- fast -- but the 8711 answers the
 * command only after it has written the whole image to its own flash, which is tens
 * of seconds.  The App therefore sees 100%, then a silence, then 0xE5 or 0xE6.
 * Inventing a second meaning for the counters (restarting from zero, or a total of
 * 2x the size) would misinform an App that reuses its 0x14 progress bar, so the
 * push is watchdogged locally instead; see the stall detector in the .c.
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
 *  No WAIT_CONFIRM: like the file path in V1.3, the device answers for itself, so
 *  the state would be entered and left in the same call.
 *
 *  COMPLETING exists for the same reason it does in xfer_session: the image is
 *  verified and the data plane has been told so, but the EBXR ack is a queued AT
 *  transaction and the slow work -- there, the flash commit; here, the push -- must
 *  not start until it has left.  PUSHING is entered from that ack's completion.
 *----------------------------------------------------------------------------*/
typedef enum
{
    OTA_SESSION_IDLE = 0,
    OTA_SESSION_WAIT_STA,   /* AP is up, 0x13 is out, waiting for the phone   */
    OTA_SESSION_RECV,       /* buffering the image into PSRAM                 */
    OTA_SESSION_COMPLETING, /* verified; waiting for the data-plane ack to go  */
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
 * @brief  Handle a decoded 0xE0 OTA_OFFER.  Always answers: 0xE1 DECISION(ACCEPT)
 *         followed by 0x13 AP_INFO, or 0xE6 OTA_FAIL with a §2.6 reason.
 *
 * @param  name       image label, NUL-terminated; required to match the EBXF
 *                    header's name and echoed back in 0xE5.  May be "" -- then the
 *                    header's name is not compared, exactly as for a file.
 * @param  file_type  from the offer; must be EB_FILE_TYPE_BIN.
 * @param  size       image length.  Load bearing three times: AT+OTAUSER declares
 *                    it to the module before the first byte is pushed, a raw image
 *                    has no end marker so it is how the receive phase knows it is
 *                    finished, and it is cross-checked against the EBXF header.
 * @param  crc32      whole-image CRC32, verified before anything is pushed.
 */
void ota_session_offer(const char *name, uint8_t file_type,
                       uint32_t size, uint32_t crc32);

/** The phone opened the TCP connection.  Promotes WAIT_STA -> RECV. */
void ota_session_on_sta_joined(void);

/** TCP closed.  Mid-receive this fails the session; from COMPLETING onwards it is
 *  expected -- the ack has gone out and we close the connection ourselves before
 *  pushing. */
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
/** Image bytes received over Wi-Fi so far (payload only -- the EBXF header is
 *  not counted, because the offer's size does not count it either). */
uint32_t ota_session_bytes_received(void);
/** Image size the offer declared (0 when idle). */
uint32_t ota_session_total_size(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_OTA_SESSION_H_ */
