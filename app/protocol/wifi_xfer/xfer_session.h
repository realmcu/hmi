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
 * xfer_session_on_payload().  UNREACHABLE in the current topology: the 8711
 * only forwards payloads it has validated as JPEG (`FF D8`..`FF D9`), so an
 * EBXF-prefixed stream is refused with `ERR <seq> JPEG` before it ever gets to
 * SPI.  Kept for a future firmware that terminates TCP on this chip.
 */
void xfer_session_on_tcp_data(const uint8_t *data, uint16_t len);

/**
 * @brief  A chunk of pure file bytes -- no framing header of any kind.
 *
 * This is the live path: jpgs_ingress calls it with payload the 8711 has
 * already stripped of TCP, of the phone's "JPG <size> <seq>" line, and of the
 * JPGS slot header.  The file's identity (name / type / size / crc32) comes
 * from the BLE 0x10 offer instead, which the EBXF header was only ever
 * required to restate.
 *
 * Appends to flash, accumulates the CRC32, emits 0x14 PROGRESS, and on the
 * last byte verifies against the offer's CRC and commits.
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
