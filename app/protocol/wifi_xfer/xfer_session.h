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
 *         Otherwise stashes the offer and enters WAIT_CONFIRM (30s timeout);
 *         the app UI is expected to invoke xfer_session_user_decision().
 */
void xfer_session_offer(const char *name, uint8_t file_type,
                        uint32_t size, uint32_t crc32, uint16_t replace_id);

/** User answered the confirmation prompt: accept => raise AP; reject => fail. */
void xfer_session_user_decision(bool accept);

/** SoftAP notifies that the STA (App) has associated.  Enters RECV. */
void xfer_session_on_sta_joined(void);

/** TCP delivered a chunk of body bytes.  Handles EBXF header on first call. */
void xfer_session_on_tcp_data(const uint8_t *data, uint16_t len);

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
