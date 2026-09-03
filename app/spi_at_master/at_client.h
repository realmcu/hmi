/**
 * @file    at_client.h
 * @brief   One AT command's lifecycle: queue it, send it, collect its response,
 *          finish on OK / ERROR / timeout.  Knows no command semantics.
 *
 * Sits between at_parser (bytes -> lines) and at_wifi / at_socket (meaning).
 * Everything above this layer gets to pretend an AT command is an ordinary
 * asynchronous call with a completion callback.
 *
 * ---------------------------------------------------------------------------
 * STRICTLY ONE COMMAND IN FLIGHT.  THIS IS A HARDWARE CONSTRAINT
 * ---------------------------------------------------------------------------
 * The standard AT protocol has no sequence numbers: a response is matched to a
 * command purely by "it is the only one outstanding".  Worse, the 8711's inbound
 * ring buffer is 4 KiB and it discards an overflow SILENTLY -- so pipelining
 * does not merely confuse the response matching, it loses commands with no error
 * anywhere.  Hence the queue below: callers may submit whenever they like, and
 * exactly one command is on the wire at a time.
 *
 * ---------------------------------------------------------------------------
 * WHAT ENDS A COMMAND
 * ---------------------------------------------------------------------------
 *   "OK"                  success
 *   "ERROR: <n>"          failure, <n> reported as error_no
 *   timeout               nothing arrived in time
 *   "ATCMD READY"         the 8711 rebooted under us -- everything in flight is
 *                         gone, and reporting that as a timeout would hide the
 *                         one fact that explains it
 *
 * Any other line is response body and goes to the request's on_line callback.
 * "[$]..." lines are NOT body: they are unsolicited indications that may land
 * between two body lines, so they are routed to the event sinks instead and do
 * not disturb the command in flight.
 *
 * ---------------------------------------------------------------------------
 * TRANSPARENT TRANSMISSION IS A COMMAND KIND, NOT A MODE CALLERS ENTER
 * ---------------------------------------------------------------------------
 * at_client_send_raw() is one request from a caller's point of view, but on the
 * wire it is a four-step dance: the command declaring <size>, the 8711's ">>>",
 * exactly <size> payload bytes, then "OK".  There is no escape sequence -- the
 * 8711 counts bytes and leaves transparent mode when it has them all.
 *
 * This lives here rather than in at_socket because every step is a
 * command-lifecycle step, and because a half-finished dance leaves the 8711 in
 * transparent mode where the next ordinary command would be interpreted as
 * payload.  Keeping it inside the single-flight queue is what makes that
 * unrepresentable; whichever way a payload ends up short -- the request timed out,
 * or the transport dropped one of its frames -- the 8711's three-'<'-frame abort
 * goes out so it cannot poison the commands after it.
 *
 * A raw request that ends in AT_RES_TIMEOUT or AT_RES_SEND_FAIL therefore means
 * "the payload did not reach the socket, and transparent mode has been unstuck" --
 * an abort's own "OK" is not reported as success.
 *
 * CONTEXT: submit from any thread.  Callbacks run on the SPI transport thread
 * (for anything driven by an inbound line) or on the system workqueue (for
 * timeouts and staging retries) -- never on l2_task.  Anything touching protocol
 * state must hop over with ebadge_task_post_call().
 */
#ifndef _AT_CLIENT_H_
#define _AT_CLIENT_H_

#include <stdint.h>
#include <stdbool.h>

#include "at_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest command line a caller may submit.
 *
 *  Well under the 8711's own 1999 B line limit (its ROM shell's
 *  UART_LOG_CMD_BUFLEN is 2000, and the 16 KiB SPI slot does NOT relax that),
 *  and sized for the longest command this firmware actually builds:
 *  AT+WLSTARTAP with a 32-byte SSID and a 64-byte password is about 130 B.
 *  Queue depth multiplies this, so it is not free. */
#define AT_CLIENT_CMD_MAX       224

/** Largest transparent-mode payload that is COPIED into the request.  EBXR is
 *  8 bytes; the margin is for a future short reply, not for bulk transmit.
 *
 *  Bulk uplink does exist now, and it deliberately does not go through here: an
 *  OTA image is megabytes and copying it into a queued request would need the
 *  buffer twice.  See at_client_submit_stream(), which borrows the caller's
 *  buffer instead. */
#define AT_CLIENT_RAW_MAX       64

/** Requests that may wait behind the one in flight.
 *
 *  Four covers the deepest real burst (AP up, then two AT+SKTSERVER, then a
 *  state query).  A full queue returns -ENOSPC rather than blocking, because
 *  every caller here is either l2_task or a work item and neither may sleep. */
#define AT_CLIENT_QUEUE_DEPTH   4

/** Event sinks fanned out to.  Two are used (at_wifi for the client-join
 *  indications, at_socket for the [$][SKT] ones); the rest is headroom. */
#define AT_CLIENT_EVENT_SINKS   4

/** Reboot sinks fanned out to.  A reboot invalidates a different thing in each
 *  layer above -- the link table in at_socket, the cached AP in at_wifi, the
 *  listener flags in the port layer -- so this cannot be one sink that the last
 *  module to initialise wins. */
#define AT_CLIENT_READY_SINKS   4

/** Default per-command budget.  Generous because the slow ones really are slow:
 *  AT+WLSTARTAP answers immediately but AT+SKTSERVER binds a socket, and a
 *  scan -- which eBadge does not use but the shell can -- takes seconds. */
#define AT_CLIENT_TIMEOUT_MS    3000U

/** Budget for the transparent-mode dance, measured from the command going out
 *  to the closing "OK".  On expiry the abort frames go out and the request is
 *  reported as AT_RES_TIMEOUT a further ~300 ms later. */
#define AT_CLIENT_TT_TIMEOUT_MS 2000U

/*----------------------------------------------------------------------------*
 *  Streamed transparent-mode defaults  (at_client_submit_stream)
 *
 *  Three budgets rather than one, because a megabyte-long push has three
 *  genuinely different waits in it and a single number would have to be the
 *  worst of them -- which would then also apply to the ones that should fail
 *  fast.
 *----------------------------------------------------------------------------*/

/** Command line out -> ">>>" back.  Generous because the command that opens the
 *  stream does real work first: AT+OTAUSER allocates its buffer and initialises
 *  the OTA context (which touches the 8711's own flash) before it prompts. */
#define AT_CLIENT_STREAM_PROMPT_MS  10000U

/** How long the TRANSPORT waits for S2M while a stream is in flight, replacing
 *  its 1000 ms default for the duration.
 *
 *  1000 ms is not enough and the bench proved it: the 8711 took the first 16 KiB
 *  chunk, went away to erase flash, and came back late -- the transport had
 *  already abandoned chunk two, so the image would have had a hole in it and the
 *  push failed with SEND_FAIL at 32752 B.  15 s is a guess at the worst case (a
 *  whole-partition erase up front) rather than a measurement, deliberately on
 *  the generous side: too short brings the update down, too long only delays the
 *  report of a peer that is genuinely dead, and only during a push. */
#define AT_CLIENT_STREAM_READY_MS   15000U

/** One 16 KiB chunk staged -> the next one staged.  Covers the 8711 stalling
 *  mid-stream on its own flash write, which it must do: it cannot buffer a whole
 *  image.  Must stay above AT_CLIENT_STREAM_READY_MS or a chunk the transport
 *  abandoned would look like a chunk we failed to produce. */
#define AT_CLIENT_STREAM_CHUNK_MS   20000U

/** Last byte out -> "OK"/"ERROR".  The 8711 finalises the image here, so this is
 *  the longest wait in the sequence by far.  The only cost of it being generous
 *  is how long a dead peer takes to be reported. */
#define AT_CLIENT_STREAM_TERM_MS    60000U

/*----------------------------------------------------------------------------*
 *  Results
 *----------------------------------------------------------------------------*/
typedef enum
{
    AT_RES_OK = 0,      /**< terminated on "OK"                               */
    AT_RES_ERROR,       /**< terminated on "ERROR: <n>"; see error_no          */
    AT_RES_TIMEOUT,     /**< nothing terminated it in time                     */
    AT_RES_RESET,       /**< "ATCMD READY" arrived -- the 8711 restarted       */
    AT_RES_SEND_FAIL,   /**< could not get the line onto the wire at all       */
} at_result_t;

/** The spelling of @p res, for logs.  Never NULL. */
const char *at_result_str(at_result_t res);

/**
 * @brief  One line of the response body -- not a terminator, not an event.
 *
 * Called in arrival order, NUL-terminated, CR/LF stripped.  A command with no
 * body (most of them) never sees this.
 */
typedef void (*at_line_cb_t)(const char *line, uint16_t len, void *user);

/**
 * @brief  The command finished, exactly once, whatever the outcome.
 *
 * @param  error_no  the <n> from "ERROR: <n>" when @p res is AT_RES_ERROR,
 *                   otherwise 0.  Worth reporting: "command does not exist" and
 *                   "socket refused" arrive by the same route and only this
 *                   number separates them.
 */
typedef void (*at_done_cb_t)(at_result_t res, int error_no, void *user);

/** Unsolicited "[$]..." indication.  Fanned out to every registered sink, in
 *  registration order, with the "[$]" prefix still attached. */
typedef void (*at_event_cb_t)(const char *line, uint16_t len, void *user);

/** Raw socket bytes from a [$][SKT][DATA] event -- same contract as
 *  at_parser_data_cb_t, forwarded verbatim.  One sink only: the routing by
 *  link_id belongs to at_socket, which is that sink. */
typedef at_parser_data_cb_t at_data_cb_t;

/** The 8711 announced itself with "ATCMD READY".  Fires on every occurrence, so
 *  a caller must treat it as "everything you thought you knew is stale" rather
 *  than as a one-shot boot signal. */
typedef void (*at_ready_cb_t)(void *user);

/*----------------------------------------------------------------------------*
 *  Lifecycle
 *----------------------------------------------------------------------------*/

/**
 * @brief  Bring up the transport, install the parser, start accepting commands.
 *
 * Calls spi_at_master_init() itself and takes over its RX callback, so nothing
 * else may install one afterwards.  Idempotent.
 *
 * Returns 0 even though the 8711 may not have said "ATCMD READY" yet -- see
 * at_client_link_ready().  A command submitted before then is queued, not
 * refused: on a cold boot both chips come up together and refusing would make
 * startup order load-bearing.
 */
int at_client_init(void);

/** True once "ATCMD READY" has been seen at least once. */
bool at_client_link_ready(void);

/** True while a command is on the wire (queued-but-not-sent does not count). */
bool at_client_busy(void);

/*----------------------------------------------------------------------------*
 *  Submitting
 *----------------------------------------------------------------------------*/

/**
 * @brief  Queue a command line.  CRLF is appended here; do not include it.
 *
 * @param  cmd        NUL-terminated, < AT_CLIENT_CMD_MAX
 * @param  on_line    response body sink, or NULL to discard the body
 * @param  done       completion, called exactly once; NULL is legal for
 *                    fire-and-forget
 * @param  user       passed to both callbacks
 * @param  timeout_ms 0 selects AT_CLIENT_TIMEOUT_MS
 *
 * @retval 0         queued (NOT sent, and certainly not answered)
 * @retval -ENODEV   at_client_init() never succeeded
 * @retval -EMSGSIZE @p cmd too long
 * @retval -ENOSPC   queue full
 */
int at_client_submit(const char *cmd, at_line_cb_t on_line, at_done_cb_t done,
                     void *user, uint32_t timeout_ms);

/**
 * @brief  Queue a command whose text is built from a printf format.
 *
 * The common case: every AT+SKT* and AT+WL* call site would otherwise keep its
 * own snprintf buffer, and those buffers live on l2_task's stack.
 *
 * Truncation is an error (-EMSGSIZE), not a silent short command: a truncated
 * AT+SKTSERVER is a syntactically valid command for a different port.
 */
int at_client_submitf(at_line_cb_t on_line, at_done_cb_t done, void *user,
                      uint32_t timeout_ms, const char *fmt, ...);

/**
 * @brief  Send @p len raw bytes over connection @p link using AT+SKTSENDRAW and
 *         transparent transmission.
 *
 * The only binary-safe way to put bytes on a socket: AT+SKTSEND embeds them in
 * the command line, where the 8711's shell truncates at the first 0x00 -- and
 * an EBXR ack always contains 0x00 (its reserved field, and its reason byte on
 * success).
 *
 * REQUIRES Host Control Mode on the 8711.  Without it the command answers
 * "ERROR: <n>" and this reports AT_RES_ERROR, which is why it is worth issuing
 * one throwaway AT+SKTSENDRAW during bring-up rather than discovering the
 * missing menuconfig option on the first real transfer.
 *
 * @param  data  copied into the request, so the caller's buffer is free
 *               immediately.  Max AT_CLIENT_RAW_MAX.
 */
int at_client_send_raw(uint8_t link, const uint8_t *data, uint16_t len,
                       at_done_cb_t done, void *user);

/*----------------------------------------------------------------------------*
 *  Streamed transparent transmission
 *
 *  at_client_send_raw() above and this are the same 8711-side dance -- command,
 *  ">>>", exactly <length> bytes, "OK" -- and they are separate functions
 *  because the two ends of the size range want opposite things:
 *
 *    send_raw    copies, one frame, one timeout.  Right for 8 bytes; would need
 *                a second megabyte of PSRAM for an OTA image.
 *    this        borrows, N frames, a timeout per frame.  Right for an image;
 *                pointless bookkeeping for 8 bytes.
 *
 *  The command line is the CALLER's here, not hardcoded as AT+SKTSENDRAW: the
 *  only thing this layer knows about a transparent-mode command is that it
 *  prompts and then counts bytes, and AT+OTAUSER does exactly that with a
 *  different name.  Getting the declared length to match @p len is the caller's
 *  job for the same reason -- this cannot know which argument of the command
 *  line is the count.
 *----------------------------------------------------------------------------*/

/** Progress notification while a stream is being pushed.
 *
 *  Called after each chunk has been handed to the transport, so @p sent is
 *  "given to the wire", not "acknowledged by the 8711" -- the 8711 acknowledges
 *  nothing until the closing "OK".  Fires at most once per ~16 KiB.
 *
 *  CONTEXT: the same as at_done_cb_t -- a work-queue thread, never l2_task. */
typedef void (*at_stream_cb_t)(uint32_t sent, uint32_t total, void *user);

typedef struct
{
    /** The bytes to push.  BORROWED, not copied: it must stay valid and
     *  unmodified until @p done fires.  A caller that reuses this buffer for the
     *  next transfer has to wait for that callback, not for the submit. */
    const uint8_t *data;
    uint32_t       len;             /**< must equal the command's <length>      */

    at_stream_cb_t on_progress;     /**< may be NULL                            */
    at_done_cb_t   done;            /**< may be NULL, but see the note below     */
    void          *user;

    uint32_t       prompt_ms;       /**< 0 -> AT_CLIENT_STREAM_PROMPT_MS         */
    uint32_t       chunk_ms;        /**< 0 -> AT_CLIENT_STREAM_CHUNK_MS          */
    uint32_t       term_ms;         /**< 0 -> AT_CLIENT_STREAM_TERM_MS           */
} at_stream_req_t;

/**
 * @brief  Queue @p cmd and then push @p req->len bytes to the 8711 in
 *         transparent mode, one 16 KiB slot at a time.
 *
 * REQUIRES Host Control Mode on the 8711, exactly as at_client_send_raw() does.
 *
 * ONE AT A TIME, AND THAT IS THE QUEUE'S DOING, NOT A FLAG HERE.  at_client is
 * strictly single-flight, so a second stream simply waits -- but a caller that
 * submits two streams over the same buffer has still broken the borrow above, and
 * nothing here can detect it.  Passing NULL for @p done is therefore only safe
 * for a buffer that is never reused.
 *
 * A DROPPED CHUNK IS DETECTED, NOT ASSUMED AWAY.  spi_at_master_send() returns 0
 * for "staged" and can still abandon that frame later if the 8711 never raises
 * S2M (spi_at_master.c:ready_timeout_handler), with no callback.  Left alone that
 * would put a 16 KiB hole in the middle of an image and then hang until term_ms,
 * reported as a timeout with no hint at the cause.  So the transport's
 * ready_timeout counter is sampled per chunk, and a stream that loses one aborts
 * transparent mode and reports AT_RES_SEND_FAIL.
 *
 * AND IT IS ALSO MADE UNLIKELY.  For the duration of the stream the transport's
 * S2M patience is raised to AT_CLIENT_STREAM_READY_MS, because the peer legitimately
 * stops servicing slots while it writes the bytes it has already been given -- at
 * 1 s it was losing chunk two of an OTA push.  Every other command keeps the short
 * default, where a silent peer is a dead peer.
 *
 * @retval 0         queued
 * @retval -ENODEV   at_client_init() never succeeded
 * @retval -EINVAL   NULL @p cmd / @p req / @p req->data, or len == 0
 * @retval -EMSGSIZE @p cmd too long
 * @retval -ENOSPC   queue full
 */
int at_client_submit_stream(const char *cmd, const at_stream_req_t *req);

/*----------------------------------------------------------------------------*
 *  Unsolicited traffic
 *----------------------------------------------------------------------------*/

/** Add an event sink.  Registering the same fn twice is a no-op; there is no
 *  unregister, matching ebadge_task_set_tick(). */
void at_client_add_event_cb(at_event_cb_t cb, void *user);

/** Install the socket-data sink.  Replaces any previous one. */
void at_client_set_data_cb(at_data_cb_t cb, void *user);

/** Install the "ATCMD READY" sink.  Registering the same fn twice is a no-op;
 *  there is no unregister, matching at_client_add_event_cb(). */
void at_client_add_ready_cb(at_ready_cb_t cb, void *user);

/*----------------------------------------------------------------------------*
 *  Diagnostics
 *----------------------------------------------------------------------------*/

/** Log every inbound text line as "[8711->8773] at< <line>".
 *
 *  On by default, and worth turning off during a file transfer: the lines
 *  themselves are cheap, but they print on the transport thread with M2S
 *  asserted, so under a burst of socket events the logging is what throttles the
 *  8711.  The binary payloads are never logged here regardless -- that is
 *  spi_at_master's rx hexdump, controlled separately by `atm dump`. */
void at_client_set_log_lines(bool on);
bool at_client_get_log_lines(void);

/*----------------------------------------------------------------------------*
 *  Introspection
 *----------------------------------------------------------------------------*/
typedef struct
{
    uint32_t submitted;
    uint32_t completed_ok;
    uint32_t completed_err;
    uint32_t timeouts;
    uint32_t resets;        /**< "ATCMD READY" seen                            */
    uint32_t send_fails;    /**< could not stage the line even after retrying   */
    uint32_t queue_full;    /**< submissions refused with -ENOSPC               */
    uint32_t events;        /**< "[$]..." lines fanned out                      */
    uint32_t orphan_lines;  /**< body text with no command in flight            */
} at_client_stats_t;

void at_client_get_stats(at_client_stats_t *out);
void at_client_reset_stats(void);

/** Parser counters, for the same shell command that shows the above. */
void at_client_get_parser_stats(at_parser_stats_t *out);

/*----------------------------------------------------------------------------*
 *  Power-off
 *----------------------------------------------------------------------------*/
/**
 * @brief  Cancel the timeout and staging timers and abandon the queue.
 *
 * One-way, for the power-off path.  Both of this module's k_work_delayable are
 * live kernel timeouts that would pull the SoC back out of power-down at their
 * deadline no matter which threads are suspended -- the retry/timeout pair is
 * armed for seconds at a time, so the wake is close to guaranteed.
 *
 * In-flight commands are dropped without firing their done callbacks: those run
 * on the transport thread, which is about to be suspended, and the 8711 is being
 * powered off regardless.  Call before suspending any thread -- the cancels are
 * synchronous and would deadlock against a suspended system workqueue.
 */
void at_client_quiesce(void);

#ifdef __cplusplus
}
#endif

#endif /* _AT_CLIENT_H_ */
