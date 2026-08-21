/**
 * @file    wifi_8711_at.h
 * @brief   ATMC transaction layer -- one AT command in, one response out.
 *
 * SCOPE: this layer owns the ATMC half of the slot protocol and nothing else.
 * It builds COMMAND slots, matches RESPONSE slots against the sequence it sent,
 * swallows POLL heartbeats, times out a command that is never answered, and
 * restores idle TX content when a transaction ends.  It does NOT know what any
 * particular command means -- "AT+WLSTATE" is just a string to it.  The
 * semantic layer (SSID / password / client count) is wifi_8711_at_ap.h.
 *
 * It sits on top of wifi_8711_xfer.h, which moves 4096-byte slots, and below
 * wifi_8711_at_ap.h + the protocol stack's port_softap.
 *
 *      port_softap / cmd_get_ap_info      <- protocol stack, l2_task
 *          |
 *      wifi_8711_at_ap.h                  <- text -> struct, AP cache
 *          |
 *      wifi_8711_at.h                     <- THIS FILE: command/response
 *          |
 *      wifi_8711_xfer.h                   <- 4096 B slots, B2W/W2B handshake
 *
 * ---------------------------------------------------------------------------
 * WHY EVERY CALL IS ASYNCHRONOUS
 * ---------------------------------------------------------------------------
 * The 8711 is the SPI master: it owns SCLK and CS, and this side cannot
 * transmit at will -- it can only *stage* a slot and wait to be clocked.  A
 * command therefore needs one transaction to go out and another to bring the
 * reply back.  While the link is idle the 8711 polls roughly every 2 s, so the
 * reply lands 2..4 s after the call; during a JPEG stream every JPGS slot also
 * carries MISO, so a staged command rides along and the same exchange completes
 * in milliseconds (protocol sec.4.1 item 3, sec.12).
 *
 * There is no way to shorten the idle case from this side.  B2W is a readiness
 * gate, not a request line: sec.11.1 requires it to be raised on every re-arm
 * regardless of whether the TX slot holds a command, so the act of raising it
 * cannot encode "I have data".  Using it as a data-available notification is a
 * documented 8711 firmware change (v1 spec sec.9.1, "当前 AT 通道未用"), not
 * something this file can arrange.
 *
 * Hence: wifi_8711_at_submit() returns as soon as the command is staged.  0
 * means "queued", NOT "the 8711 answered".
 *
 * ---------------------------------------------------------------------------
 * SINGLE FLIGHT
 * ---------------------------------------------------------------------------
 * Exactly one command may be outstanding.  This is not a simplification, it is
 * forced by the wire: there is one TX slot, so staging a second command would
 * overwrite the first before it was ever clocked out, and the 8711 answers with
 * a bare Sequence that only identifies one of them.  A submit while busy is
 * refused with -EBUSY rather than queued, so the caller finds out immediately
 * instead of having its command silently dropped.
 */
#ifndef _WIFI_8711_AT_H_
#define _WIFI_8711_AT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How long a command may go unanswered before the callback fires TIMEOUT.
 *
 *  Four idle POLL periods.  The protocol floor is two (one transaction to carry
 *  the COMMAND out, one to bring the RESPONSE back), so this leaves room for a
 *  missed poll without reporting a failure the link would have recovered from
 *  on its own. */
#define WIFI_8711_AT_TIMEOUT_MS   8000U

/** Longest response text handed to a callback, including the NUL.
 *
 *  A RESPONSE payload may be up to 4064 B, but the only commands that exist
 *  answer with a few lines of "KEY=value".  512 B holds a WLSTATE reply with
 *  several associated clients; anything longer is truncated and flagged through
 *  @ref wifi_8711_at_result_t so the caller never silently parses half a
 *  reply. */
#define WIFI_8711_AT_TEXT_MAX     512U

typedef enum
{
    WIFI_8711_AT_OK = 0,        /**< response received and copied out         */
    WIFI_8711_AT_ERR_TIMEOUT,   /**< nothing came back inside the deadline    */
    WIFI_8711_AT_ERR_PEER,      /**< the 8711 answered "[AT]:ERROR"           */
    WIFI_8711_AT_ERR_TRUNC,     /**< reply was longer than the text buffer    */
    WIFI_8711_AT_ERR_ABORT,     /**< transaction dropped (link torn down)     */
} wifi_8711_at_result_t;

/**
 * @brief  Completion of one AT transaction.
 *
 * @param  res   outcome; @p text is only meaningful for OK and TRUNC
 * @param  text  NUL-terminated reply, valid only for the duration of the call
 * @param  len   strlen(text)
 * @param  user  the pointer handed to wifi_8711_at_submit()
 *
 * CONTEXT: the transport thread on the success path, the system workqueue on
 * the timeout path.  Never an ISR, so logging and locking are allowed -- but it
 * is NOT the protocol stack's l2_task, so anything that touches protocol state
 * must marshal itself across with ebadge_task_post_call().
 *
 * It must also return promptly: on the success path B2W stays low for the whole
 * callback, which back-pressures the 8711.
 */
typedef void (*wifi_8711_at_cb_t)(wifi_8711_at_result_t res, const char *text,
                                  size_t len, void *user);

/**
 * @brief  Sink for JPGS (JPEG stream) slots seen on the link.
 *
 * The ATMC sink installed by this layer is the only slot sink the transport
 * has, so JPGS slots would otherwise be dropped on the floor.  Registering one
 * here is how the display path gets them without a second sink existing.
 *
 * @param  slot  the whole 4096-byte slot, JPGS header included
 * @param  len   always WIFI_8711_SLOT_SIZE
 *
 * Same context and promptness rules as wifi_8711_at_cb_t.
 */
typedef void (*wifi_8711_jpg_sink_t)(const uint8_t *slot, size_t len);

/*----------------------------------------------------------------------------*
 *  Lifecycle
 *
 *  There is no init function, deliberately.  This layer installs its slot sink
 *  and starts the transport from the first wifi_8711_at_submit(), because an
 *  init that started the transport at boot would raise B2W and invite the 8711
 *  to clock slots before anything wanted them -- and an init that did NOT start
 *  it would do nothing at all while implying callers must order around it.
 *
 *  Consequence worth knowing: JPGS slots only reach the sink below once the
 *  transport is running, so nothing is received until the first AT command has
 *  been submitted by somebody.  port_softap's boot-time AP query is what does
 *  that in practice.
 *----------------------------------------------------------------------------*/

/**
 * @brief  Install the JPGS slot sink (or NULL to drop stream frames).
 *
 * The ATMC sink this layer installs is the only slot sink the transport has, so
 * JPGS slots would otherwise be dropped on the floor.  Registering one here is
 * how the display path gets them without a second sink existing.
 */
void wifi_8711_at_set_jpg_sink(wifi_8711_jpg_sink_t cb);

/*----------------------------------------------------------------------------*
 *  Transactions
 *----------------------------------------------------------------------------*/

/**
 * @brief  Stage one AT command and arrange for @p cb to be called with the
 *         reply.
 *
 * Starts the slot transport if it is not running yet.  The command is staged
 * BEFORE the transport starts where possible, because staged content is only
 * promoted into the DMA buffer at an ARM: staging second would put an all-zero
 * slot on the wire first and cost a whole POLL period for nothing.
 *
 * @param  cmd   AT command text including the trailing CRLF, e.g.
 *               SPI_AT_CMD_WLSTATE.  Must be shorter than 128 B -- the 8711's
 *               parse buffer is that size and silently drops anything longer.
 * @param  cb    completion callback; may be NULL to fire-and-forget
 * @param  user  opaque, handed back to @p cb
 *
 * @retval 0          staged; @p cb fires in ~2..4 s on an idle link
 * @retval -EBUSY     another command is already outstanding (single flight)
 * @retval -ENODEV    wifi_8711_init() never succeeded -- no link on this build
 * @retval -EMSGSIZE  command text >= 128 B
 * @retval <0         transport start or staging error
 */
int wifi_8711_at_submit(const char *cmd, wifi_8711_at_cb_t cb, void *user);

/** True while a command is outstanding.  A submit now would return -EBUSY. */
bool wifi_8711_at_busy(void);

/**
 * @brief  Abandon the outstanding command, firing its callback with ABORT.
 *
 * For teardown paths that must not leave a caller waiting for a reply that can
 * no longer arrive.  A no-op when idle.
 */
void wifi_8711_at_abort(void);

/*----------------------------------------------------------------------------*
 *  Statistics -- the bring-up instrument
 *
 *  Separate counters rather than one error total, because each answers a
 *  different "which half is broken" question.
 *----------------------------------------------------------------------------*/
typedef struct
{
    uint32_t submits;       /**< commands staged                              */
    uint32_t responses;     /**< RESPONSE slots matching our sequence         */
    uint32_t stale;         /**< RESPONSE slots for some older sequence       */
    uint32_t timeouts;      /**< commands that were never answered            */
    uint32_t polls;         /**< POLL heartbeats -- proof of life             */
    uint32_t jpg_slots;     /**< JPGS slots seen                              */
    uint32_t bad_magic;     /**< slots that were neither ATMC nor JPGS        */
    uint32_t parse_err;     /**< ATMC slots that failed header/CRC validation */
} wifi_8711_at_stats_t;

void wifi_8711_at_get_stats(wifi_8711_at_stats_t *out);
void wifi_8711_at_reset_stats(void);

/** Human-readable name for a result code, for logging. */
const char *wifi_8711_at_result_str(wifi_8711_at_result_t res);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_8711_AT_H_ */
