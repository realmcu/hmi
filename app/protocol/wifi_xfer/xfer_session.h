/**
 * @file    xfer_session.h
 * @brief   Single-flight file transfer state machine (BLE control + TCP data).
 *
 * Owns:
 *   - the state (IDLE / WAIT_CONFIRM / WAIT_STA / RECV / COMPLETING)
 *   - timeout deadlines (driven by ebadge_task tick)
 *   - the streaming CRC32 accumulator and byte counter
 *   - the storage write handle (opaque)
 *
 * All entry points below must be called on l2_task context (via the frame
 * handlers or ebadge_task_post_call).  There are no locks.
 */
#ifndef _EBADGE_XFER_SESSION_H_
#define _EBADGE_XFER_SESSION_H_

#include <stdint.h>
#include <stdbool.h>

#include "ebxf_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  Phase (also used for UI progress)
 *----------------------------------------------------------------------------*/
typedef enum
{
    XFER_SESSION_IDLE = 0,
    XFER_SESSION_WAIT_CONFIRM,
    XFER_SESSION_WAIT_STA,
    XFER_SESSION_RECV,
    XFER_SESSION_COMPLETING,
} xfer_session_state_t;

/*----------------------------------------------------------------------------*
 *  Lifecycle
 *----------------------------------------------------------------------------*/
/** Register the session with ebadge_task tick.  Called at startup. */
void xfer_session_init(void);

/** Return the current state (for progress queries). */
xfer_session_state_t xfer_session_state(void);

/*----------------------------------------------------------------------------*
 *  Entry points  (all on l2_task)
 *----------------------------------------------------------------------------*/
/**
 * @brief  Handle a decoded 0x10 XFER_OFFER.  Runs the pre-checks:
 *
 *          - already busy?     -> emit 0x16 FAIL(BUSY), state unchanged
 *          - unsupported type? -> emit 0x16 FAIL(UNSUP_TYPE)
 *          - no space?         -> emit 0x16 FAIL(STORAGE_FULL)
 *
 *         If none of those fire, V1.3 §4.7 requires an immediate automatic
 *         accept ("设备不弹窗，自动回复同意或拒绝"), so this calls
 *         xfer_session_user_decision(true) itself and returns with the AP
 *         coming up.  WAIT_CONFIRM is therefore transient in V1.3.
 */
void xfer_session_offer(const char *name, uint8_t file_type,
                        uint32_t size, uint32_t crc32, uint16_t replace_id);

/**
 * @brief  Resolve WAIT_CONFIRM: accept => raise AP; reject => 0x16 + reset.
 *
 * V1.3 §4.7 dropped the confirmation UI, so xfer_session_offer() now invokes
 * this itself with accept=true.  It stays public because §5.7 still defines
 * the state and because reinstating a prompt is then a one-line change.
 */
void xfer_session_user_decision(bool accept);

/** SoftAP notifies that the STA (App) has associated.  Enters RECV. */
void xfer_session_on_sta_joined(void);

/**
 * @brief  A chunk arrived on a stream that still carries the 40B EBXF header.
 *
 * Consumes the header on the first call(s), then behaves as
 * xfer_session_on_payload().  UNREACHABLE in the current topology: the file
 * arrives as EBFS slots, and the 8711 has already parsed the phone's EBXF header
 * to build them -- so the header never reaches us as byte-stream bytes.  Kept
 * for a firmware that terminates TCP on this chip.
 *
 * The identity cross-check the spec attaches to this header is NOT skipped; it
 * moved to xfer_session_check_identity(), which the EBFS path calls with the
 * fields the slot header restates.
 */
void xfer_session_on_tcp_data(const uint8_t *data, uint16_t len);

/**
 * @brief  Does the data plane describe the same file the BLE offer did?
 *
 * Spec §5.2 rule 3: the transport header must restate the offer's size, crc32,
 * file type and name.  Any disagreement means the two planes describe different
 * files -- which is not a corrupt transfer but a confused one, and no amount of
 * further data can resolve it.
 *
 * @param  size       Total Size from the data plane
 * @param  crc32      whole-file CRC32 from the data plane
 * @param  file_type  file type byte from the data plane
 * @param  name       file name from the data plane, NUL-terminated; NULL skips
 *                    the name comparison
 *
 * @retval true   the planes agree; carry on receiving
 * @retval false  they do not.  The session has ALREADY been failed and torn
 *                down by the time this returns -- the stream is cut and 0x16
 *                emitted -- so a caller must simply stop touching it.
 *
 * Safe to call more than once (later chunks restate the same fields, and the
 * spec requires them to be identical, so re-checking costs nothing and catches a
 * sender that changes its mind mid-file).  Returns false if no session is in
 * RECV, because an unsolicited file is also one we cannot place.
 */
bool xfer_session_check_identity(uint32_t size, uint32_t crc32,
                                 uint8_t file_type, const char *name);

/**
 * @brief  A chunk of pure file bytes -- no framing header of any kind.
 *
 * This is the live path: ebfs_ingress calls it with payload the 8711 has already
 * stripped of TCP, of the phone's EBXF header, and of the EBFS slot header.
 *
 * Appends to flash, accumulates the CRC32, emits 0x14 PROGRESS, and on the last
 * byte verifies the whole-file CRC against the offer, commits to flash, and
 * reports the result to the data plane (which is what lets the phone's upload
 * end with a code rather than a bare disconnect).
 */
void xfer_session_on_payload(const uint8_t *data, uint16_t len);

/** TCP closed for any reason. */
void xfer_session_on_tcp_close(int reason /* ebadge_tcp_close_reason_t */);

/** GAP disconnected -- abort any in-flight transfer. */
void xfer_session_abort(void);

/*----------------------------------------------------------------------------*
 *  Introspection  (for GET_STORAGE or UI)
 *----------------------------------------------------------------------------*/
uint32_t xfer_session_bytes_received(void);
uint32_t xfer_session_total_size(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_XFER_SESSION_H_ */
