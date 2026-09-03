/**
 * @file    at_client.c
 * @brief   The single-flight command engine described in at_client.h.
 *
 * Three things happen here and they are deliberately separate:
 *
 *   rx_slot()/on_line()   inbound classification, on the SPI transport thread
 *   pump()/start_req()    outbound scheduling, from whichever thread finished
 *                         the previous command
 *   stage_work()          getting one frame onto the wire when the transport is
 *                         still busy with the previous one
 *
 * That last one is the only non-obvious piece.  spi_at_master_send() holds ONE
 * staged frame and refuses a second with -EBUSY until the 8711 raises S2M, the
 * slot has been clocked, and M2S has been released again -- the last of those is
 * not pedantry, it is what stops one request from erasing the next one's line
 * (s_xfer_busy in spi_at_master.c).  A plain command needs one frame and never
 * notices, but a transparent-mode request needs two back-to-back (the command,
 * then the payload once ">>>" arrives) and the abort path below needs three more.
 * Each has to wait for its predecessor, and that wait is unbounded from this side
 * because it depends on the peer.  Hence a retry work item rather than a loop:
 * nothing here may sleep, since callers are l2_task and work items.
 *
 * ---------------------------------------------------------------------------
 * TRANSPARENT MODE ENDS ON A BYTE COUNT, NOT ON AN ESCAPE SEQUENCE
 * ---------------------------------------------------------------------------
 * AT+SKTSENDRAW=<link>,<size> declares <size> up front.  atcmd_tt_mode_get()
 * returns as soon as that many bytes have arrived, atcmd_tt_mode_end() clears
 * the mode and the handler prints "OK".  So the normal path is: command,
 * ">>>", exactly <size> bytes, "OK".  Nothing is sent to leave the mode.
 *
 * The three 1-byte '<' frames at_intf_spi.c matches on (recv_len == 1 &&
 * payload[0] == '<', three times, hence uncoalescable) are the ABORT path -- for
 * a host that finds it cannot deliver all <size> bytes.  Sending them after a
 * complete payload is actively harmful: whether the 8711 has already left
 * transparent mode is a race we cannot win, and on the losing side those bytes
 * go into at_spi_rx_ring_buf and prefix the next command line, so the following
 * command arrives as "<AT+..." and fails.  They are therefore issued from
 * exactly one function here -- begin_tt_abort(), which refuses to run unless the
 * payload is genuinely short -- and reached from the three ways a payload can end
 * up short: the request timed out, the transport dropped a chunk of it, or a
 * chunk could not be staged inside its budget.
 */
#if defined(CONFIG_SPI_AT_MASTER)

#include <zephyr/kernel.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "at_client.h"
#include "spi_at_master.h"
#include "../protocol/ebadge_log.h"

/*----------------------------------------------------------------------------*
 *  Terminators and markers, spelled out once
 *----------------------------------------------------------------------------*/
#define LINE_OK         "OK"
#define LINE_ERROR      "ERROR"
#define LINE_PROMPT     ">>>"
#define LINE_READY      "ATCMD READY"

/* Abort-transparent-mode byte, and how many independent frames of it the 8711
 * requires.  Both fixed by at_intf_spi.c -- see the file header for why these are
 * only ever sent from begin_tt_abort(). */
#define TT_ABORT_BYTE   '<'
#define TT_ABORT_FRAMES 3

/* How long to keep retrying one frame when the transport is still busy.
 *
 * 5 ms because that is roughly the round trip of one 16 KiB slot at 25 MHz plus
 * the peer's turnaround, so retrying faster only burns work-queue slots.
 *
 * The TOTAL is not a constant: it is the transport's own S2M patience less a
 * margin, recomputed per step by stage_budget_arm(), because it has to stay
 * under that patience.  Past that point the transport abandons the earlier
 * request itself, and a retry that succeeded afterwards would be putting frame
 * N+1 on a wire that never carried frame N.  A retry COUNT used to stand in for
 * the total and was wrong twice over: it silently scaled with the tick rate, and
 * it could not follow the raised budget a stream installs. */
#define STAGE_RETRY_MS          5U
#define STAGE_RETRY_MARGIN_MS   100U

/* After the three abort frames are out, how long to keep swallowing inbound text
 * before reporting the timeout.
 *
 * The 8711 answers an abort with "OK" -- but only after xTimers_TT_Mode (30 ms)
 * has seen stop_char_cnt >= 3.  If that "OK" were still in flight when the next
 * command went out it would terminate THAT command instead, so the request is
 * held open long enough to absorb it.  300 ms covers 30 ms plus a couple of slot
 * turnarounds with room to spare. */
#define TT_ABORT_GRACE_MS   300U

/*----------------------------------------------------------------------------*
 *  Requests
 *----------------------------------------------------------------------------*/
enum { REQ_CMD = 0, REQ_RAW, REQ_STREAM };

/* True for the two kinds that put the 8711 into transparent mode, and therefore
 * for the two that must be dug back out with '<' frames if they stall. */
#define REQ_IS_TT(k)    ((k) == REQ_RAW || (k) == REQ_STREAM)

typedef struct
{
    uint8_t      kind;
    char         cmd[AT_CLIENT_CMD_MAX];
    uint8_t      raw[AT_CLIENT_RAW_MAX];
    uint16_t     raw_len;
    at_line_cb_t on_line;
    at_done_cb_t done;
    void        *user;
    uint32_t     timeout_ms;

    /* REQ_STREAM only.  `ext` is BORROWED from the caller -- the whole point of
     * this kind is not to copy megabytes into a queued request, so the buffer's
     * lifetime is the caller's promise (see at_client.h). */
    const uint8_t *ext;
    uint32_t       ext_len;
    uint32_t       ext_sent;
    at_stream_cb_t on_progress;
    uint32_t       chunk_ms;
    uint32_t       term_ms;
} at_req_t;

/* In-flight progress.  A plain command only ever sees SEND_CMD -> WAIT_TERM, a
 * raw one SEND_CMD -> WAIT_PROMPT -> SEND_PAYLOAD -> WAIT_TERM, and a stream the
 * same with SEND_PAYLOAD repeating once per 16 KiB chunk.  SEND_ABORT is off the
 * normal path entirely: begin_tt_abort() is the only way in, reached from the
 * request timeout, a chunk the transport dropped, or a stage that gave up. */
enum
{
    ST_IDLE = 0,
    ST_SEND_CMD,      /* staging the command line                             */
    ST_WAIT_PROMPT,   /* TT only: waiting for ">>>"                           */
    ST_SEND_PAYLOAD,  /* TT only: staging a payload frame                     */
    ST_SEND_ABORT,    /* recovery only: staging '<' frame `abort_done`+1      */
    ST_WAIT_TERM,     /* waiting for OK / ERROR                               */
};

/*----------------------------------------------------------------------------*
 *  State
 *----------------------------------------------------------------------------*/
static at_req_t  s_queue[AT_CLIENT_QUEUE_DEPTH];
static uint8_t   s_q_head;      /* next to dequeue                            */
static uint8_t   s_q_count;

static at_req_t  s_flight;
static uint8_t   s_step = ST_IDLE;
static uint8_t   s_abort_done;  /* '<' frames already staged, recovery only    */
static bool      s_aborting;    /* begin_tt_abort() is unsticking transparent   */

/* What settle() should report once the abort frames are out.  The abort path is
 * entered from two places now -- the timeout, and a stream chunk the transport
 * dropped -- and they must not report the same thing: one is "the 8711 said
 * nothing", the other is "the wire lost a chunk". */
static at_result_t s_pending_res = AT_RES_TIMEOUT;
static int         s_pending_err;

/* Staging budget for the current step: when the -EBUSY retries give up, and when
 * they started (which is how long the 8711 held the slot -- worth logging, since
 * that number is the whole question during an OTA push). */
static uint32_t  s_stage_started;
static uint32_t  s_stage_deadline;

/* The transport's S2M patience in force, and whether we raised it.  Tracked here
 * rather than read back so the staging budget can be derived from it. */
static uint32_t  s_ready_ms = SPI_AT_READY_TIMEOUT_MS;
static uint32_t  s_ready_prev;
static bool      s_ready_raised;

/* The transport's ready_timeout count as of the last chunk we staged.
 *
 * REQ_STREAM only, and it exists because spi_at_master_send() reporting 0 does
 * not mean the frame went out: the transport abandons a staged frame ~1 s later
 * if the 8711 never raises S2M, silently and with no callback.  Comparing this
 * across chunks is the only way to notice, and noticing matters because the
 * alternative is an image with a 16 KiB hole in it. */
static uint32_t  s_stream_ready_to;

static struct k_mutex s_lock;
static struct k_work_delayable s_timeout_work;
static struct k_work_delayable s_stage_work;

static at_parser_t s_parser;

static struct
{
    at_event_cb_t cb;
    void         *user;
} s_events[AT_CLIENT_EVENT_SINKS];

static at_data_cb_t  s_data_cb;
static void         *s_data_user;

static struct
{
    at_ready_cb_t cb;
    void         *user;
} s_readys[AT_CLIENT_READY_SINKS];

static bool s_inited;
static bool s_link_ready;
static bool s_log_lines = true;

static at_client_stats_t s_stats;

/*----------------------------------------------------------------------------*
 *  Small helpers
 *----------------------------------------------------------------------------*/
const char *at_result_str(at_result_t res)
{
    switch (res)
    {
    case AT_RES_OK:        return "OK";
    case AT_RES_ERROR:     return "ERROR";
    case AT_RES_TIMEOUT:   return "TIMEOUT";
    case AT_RES_RESET:     return "RESET";
    case AT_RES_SEND_FAIL: return "SEND_FAIL";
    default:               return "?";
    }
}

/* "ERROR: 3" -> 3.  Returns 0 when there is no number, which is honest: the
 * firmware does emit a bare "ERROR" for some paths and inventing an error code
 * for it would be worse than reporting none. */
static int parse_error_no(const char *line, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        if (line[i] >= '0' && line[i] <= '9')
        {
            return atoi(&line[i]);
        }
    }
    return 0;
}

static void pump(void);

/* (Re)start the staging budget for the step about to be attempted.  Called on
 * every step change, so each frame gets the full allowance rather than what the
 * previous one left over. */
static void stage_budget_arm(void)
{
    uint32_t now    = k_uptime_get_32();
    uint32_t budget = (s_ready_ms > STAGE_RETRY_MARGIN_MS)
                      ? (s_ready_ms - STAGE_RETRY_MARGIN_MS) : STAGE_RETRY_MS;

    s_stage_started  = now;
    s_stage_deadline = now + budget;
}

/* True while the staging retries still have time left.  Signed difference so the
 * 32-bit uptime wrapping (every ~49 days) cannot turn into an instant give-up. */
static bool stage_budget_left(void)
{
    return (int32_t)(k_uptime_get_32() - s_stage_deadline) < 0;
}

/* Give the 8711 the extra patience a stream needs, and take it back afterwards.
 * Scoped to the request rather than left on: 1 s is the right answer for every
 * other command, where a silent peer means a dead peer.  See
 * AT_CLIENT_STREAM_READY_MS. */
static void ready_budget_raise(uint32_t ms)
{
    if (s_ready_raised || ms == 0U)
    {
        return;
    }
    s_ready_prev   = spi_at_master_set_ready_timeout(ms);
    s_ready_ms     = ms;
    s_ready_raised = true;
}

static void ready_budget_restore(void)
{
    if (!s_ready_raised)
    {
        return;
    }
    (void)spi_at_master_set_ready_timeout(s_ready_prev);
    s_ready_ms     = s_ready_prev;
    s_ready_raised = false;
}

/* Finish the in-flight request.  The callback is read out and the slot cleared
 * BEFORE it is invoked, so a done handler that submits the next command sees an
 * idle engine rather than a half-torn-down one -- the same discipline
 * ebadge_port_tcp.c's ctrl_settle() uses, and for the same reason. */
static void settle(at_result_t res, int error_no)
{
    at_done_cb_t done;
    void        *user;

    k_mutex_lock(&s_lock, K_FOREVER);
    if (s_step == ST_IDLE)
    {
        k_mutex_unlock(&s_lock);
        return;
    }
    done   = s_flight.done;
    user   = s_flight.user;
    s_step = ST_IDLE;
    s_aborting = false;
    k_mutex_unlock(&s_lock);

    (void)k_work_cancel_delayable(&s_timeout_work);
    (void)k_work_cancel_delayable(&s_stage_work);
    ready_budget_restore();

    switch (res)
    {
    case AT_RES_OK:        s_stats.completed_ok++;  break;
    case AT_RES_ERROR:     s_stats.completed_err++; break;
    case AT_RES_TIMEOUT:   s_stats.timeouts++;      break;
    case AT_RES_SEND_FAIL: s_stats.send_fails++;    break;
    default: break;
    }

    if (res != AT_RES_OK)
    {
        EBADGE_WARN("at: \"%s\" -> %s(%d)", s_flight.cmd, at_result_str(res),
                    error_no);
    }

    if (done != NULL)
    {
        done(res, error_no, user);
    }

    pump();
}

/*----------------------------------------------------------------------------*
 *  Getting frames onto the wire
 *----------------------------------------------------------------------------*/

/* Bytes the next stream chunk will carry.  One definition, called from both
 * stage_current() and stage_advance(), because the two must agree exactly: the
 * first decides what goes on the wire and the second decides how far the cursor
 * moves, and a disagreement between them is a silently corrupt image.
 *
 * Read under no lock, which is safe because ext_sent is only ever written from
 * stage_advance() and staging is serialised through the single stage work item. */
static uint16_t stream_chunk_len(void)
{
    uint32_t remain = s_flight.ext_len - s_flight.ext_sent;

    return (remain > SPI_AT_PAYLOAD_MAX) ? (uint16_t)SPI_AT_PAYLOAD_MAX
           : (uint16_t)remain;
}

/* Stage whatever the current step needs.  Returns 0 when the frame is staged,
 * -EBUSY when the transport still holds the previous one, or another negative
 * errno for a permanent failure. */
static int stage_current(void)
{
    switch (s_step)
    {
    case ST_SEND_CMD:
        {
            /* Not spi_at_master_send_text(): that helper puts its scratch buffer on
             * the CALLER's stack and caps out around 256 B, and the caller here can
             * be l2_task.  The CRLF is appended into our own request buffer, which
             * is sized for it. */
            size_t n = strlen(s_flight.cmd);

            if (n + 2U > sizeof(s_flight.cmd))
            {
                return -EMSGSIZE;
            }

            uint8_t frame[AT_CLIENT_CMD_MAX + 2U];

            memcpy(frame, s_flight.cmd, n);
            frame[n++] = '\r';
            frame[n++] = '\n';
            return spi_at_master_send(frame, (uint16_t)n);
        }

    case ST_SEND_PAYLOAD:
        /* No CRLF: in transparent mode the 8711 counts bytes, and a CRLF here
         * would be two bytes of the socket's payload. */
        if (s_flight.kind == REQ_STREAM)
        {
            /* Straight out of the caller's buffer -- typically PSRAM.  Safe
             * because spi_at_master_send() memcpy's into its own staging slot
             * (spi_at_master.c:520) rather than handing the pointer to DMA, so
             * the source may live anywhere the CPU can read. */
            return spi_at_master_send(s_flight.ext + s_flight.ext_sent,
                                      stream_chunk_len());
        }
        return spi_at_master_send(s_flight.raw, s_flight.raw_len);

    case ST_SEND_ABORT:
        {
            static const uint8_t one = (uint8_t)TT_ABORT_BYTE;

            return spi_at_master_send(&one, 1U);
        }

    default:
        return -EINVAL;
    }
}

/* Enter the recovery that digs the 8711 out of transparent mode: drop whatever is
 * still staged, then stage the three '<' frames.  @p res and @p err are what
 * settle() reports once the grace period after them expires.
 *
 * Returns false when there is nothing to recover -- the request is not a
 * transparent one, its payload is already fully out, or a recovery is already
 * running -- in which case the caller reports the failure itself.
 */
static bool begin_tt_abort(at_result_t res, int err)
{
    bool go = false;

    k_mutex_lock(&s_lock, K_FOREVER);
    if (!s_aborting && REQ_IS_TT(s_flight.kind) &&
        (s_step == ST_WAIT_PROMPT || s_step == ST_SEND_PAYLOAD))
    {
        s_aborting    = true;
        s_abort_done  = 0;
        s_step        = ST_SEND_ABORT;
        s_pending_res = res;
        s_pending_err = err;
        stage_budget_arm();
        go = true;
    }
    k_mutex_unlock(&s_lock);

    if (go && spi_at_master_cancel_staged())
    {
        /* The frame we just gave up on must not go out in front of the abort: it
         * would put 16 KiB of a dead transfer into a peer that is still counting,
         * and it would hold the '<' frames back for however long that peer takes
         * to raise S2M -- which, on the path that gets here, is the whole
         * problem. */
        EBADGE_LOG("at: dropped the frame we had given up on");
    }
    if (go)
    {
        (void)k_work_reschedule(&s_stage_work, K_NO_WAIT);
    }
    return go;
}

/* Advance past a successfully staged frame. */
static void stage_advance(void)
{
    at_stream_cb_t progress    = NULL;
    void          *progress_u  = NULL;
    uint32_t       progress_n  = 0;
    uint32_t       progress_of = 0;
    bool           chunk_lost  = false;
    uint32_t       rearm_ms    = 0;
    uint32_t       held_ms     = 0;

    k_mutex_lock(&s_lock, K_FOREVER);

    switch (s_step)
    {
    case ST_SEND_CMD:
        s_step = REQ_IS_TT(s_flight.kind) ? ST_WAIT_PROMPT : ST_WAIT_TERM;
        break;

    case ST_SEND_PAYLOAD:
        if (s_flight.kind == REQ_STREAM)
        {
            /* Did the frame BEFORE this one actually go out?  The transport
             * frees its staging slot both when a frame is transmitted and when
             * it gives up on one, so a successful stage here does not by itself
             * distinguish the two -- only the counter does.
             *
             * Covers every chunk except the last, whose abandonment has nothing
             * behind it to notice and surfaces as term_ms expiring instead.
             * Acceptable: the 8711 never reaches its declared <length> in that
             * case, so it does not commit a short image either way. */
            spi_at_master_stats_t st;

            spi_at_master_get_stats(&st);
            if (s_flight.ext_sent != 0U && st.ready_timeout != s_stream_ready_to)
            {
                chunk_lost = true;
                break;      /* leave s_step alone; settle() below tears it down */
            }
            s_stream_ready_to = st.ready_timeout;

            /* How long the peer sat on the previous chunk before freeing the
             * staging slot.  Logged only when it is long, because during an OTA
             * push this is the number that decides whether the budgets above are
             * right -- and the bench has no other way to see it. */
            held_ms = k_uptime_get_32() - s_stage_started;

            s_flight.ext_sent += stream_chunk_len();

            progress    = s_flight.on_progress;
            progress_u  = s_flight.user;
            progress_n  = s_flight.ext_sent;
            progress_of = s_flight.ext_len;

            if (s_flight.ext_sent < s_flight.ext_len)
            {
                /* Stay in SEND_PAYLOAD: transparent mode does not end until the
                 * declared count is reached, so there is no intermediate reply
                 * to wait for between chunks. */
                rearm_ms = s_flight.chunk_ms;
                break;
            }
            rearm_ms = s_flight.term_ms;
        }
        /* The declared <size> bytes are out, which is what ends transparent mode
         * on the 8711 side.  Nothing further is sent -- see the file header. */
        s_step = ST_WAIT_TERM;
        break;

    case ST_SEND_ABORT:
        s_abort_done++;
        if (s_abort_done >= TT_ABORT_FRAMES)
        {
            s_step = ST_WAIT_TERM;
        }
        break;

    default:
        break;
    }

    bool more    = (s_step == ST_SEND_ABORT) || (s_step == ST_SEND_PAYLOAD);
    bool aborted = s_aborting && (s_step == ST_WAIT_TERM);

    stage_budget_arm();
    k_mutex_unlock(&s_lock);

    if (chunk_lost)
    {
        /* The abort frames DO go out here.  The earlier reading -- that a peer
         * which missed the slot has left transparent mode too -- does not survive
         * the bench: the 8711 stalls on its own flash write, comes back late, and
         * is still counting down from the declared length.  Leaving it there means
         * the next command is swallowed as image bytes, so the recovery matters
         * more on this path than on the timeout, not less. */
        EBADGE_ERR1("at: stream chunk dropped by transport at %u B -- 8711 did "
                    "not raise S2M in time", (unsigned)s_flight.ext_sent);
        if (!begin_tt_abort(AT_RES_SEND_FAIL, -EIO))
        {
            settle(AT_RES_SEND_FAIL, -EIO);
        }
        return;
    }

    if (held_ms >= 1000U)
    {
        EBADGE_WARN2("at: 8711 held the slot %u ms at %u B (flash write?)",
                     (unsigned)held_ms, (unsigned)s_flight.ext_sent);
    }

    if (progress != NULL)
    {
        progress(progress_n, progress_of, progress_u);
    }

    /* Re-armed per chunk rather than once per request: a megabyte at 16 KiB a
     * time is ~64 stages, and one budget covering all of them would have to be
     * the sum -- which is then also the delay before a stalled push is noticed. */
    if (rearm_ms != 0U && !aborted)
    {
        (void)k_work_reschedule(&s_timeout_work, K_MSEC(rearm_ms));
    }

    if (more)
    {
        (void)k_work_reschedule(&s_stage_work, K_NO_WAIT);
    }
    else if (aborted)
    {
        /* Hold the request open just long enough to absorb the "OK" the abort
         * produces (xTimers_TT_Mode needs 30 ms to notice stop_char_cnt >= 3), so
         * that "OK" cannot terminate the NEXT command instead. */
        (void)k_work_reschedule(&s_timeout_work, K_MSEC(TT_ABORT_GRACE_MS));
    }
}

static void stage_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    k_mutex_lock(&s_lock, K_FOREVER);
    bool sending = (s_step == ST_SEND_CMD) || (s_step == ST_SEND_PAYLOAD) ||
                   (s_step == ST_SEND_ABORT);
    k_mutex_unlock(&s_lock);

    if (!sending)
    {
        return;
    }

    int rc = stage_current();

    if (rc == 0)
    {
        stage_advance();
        return;
    }

    if (rc == -EBUSY && stage_budget_left())
    {
        (void)k_work_reschedule(&s_stage_work, K_MSEC(STAGE_RETRY_MS));
        return;
    }

    /* The two pad levels are here because "the peer is not answering" and "we
     * dropped our own request line" produce the same -EBUSY and the same silence,
     * and only the levels tell them apart: m2s_asserted=1 with s2m_high=0 is a
     * peer that is genuinely not answering, m2s_asserted=0 means the request was
     * never on the wire and the bug is on this side.  Logical values, so 1 means
     * asserted -- the M2S pad itself is active low.  The byte offset is here for
     * the same reason: which chunk it dies on is the difference between a flash
     * stall at a partition boundary and a link that never pipelines at all. */
    EBADGE_ERR("at: cannot stage step %u (%d) at %u/%u B after %u ms -- "
               "m2s_asserted=%d s2m_high=%d",
               (unsigned)s_step, rc, (unsigned)s_flight.ext_sent,
               (unsigned)s_flight.ext_len,
               (unsigned)(k_uptime_get_32() - s_stage_started),
               spi_at_master_get_m2s(), spi_at_master_get_s2m());

    /* Mid-payload this leaves the peer in transparent mode, exactly as a timeout
     * does, so it takes the same way out.  Note the ordering that makes this safe:
     * begin_tt_abort() moves s_step to ST_SEND_ABORT before rescheduling us, so
     * the retry that failed cannot be re-entered as itself. */
    if (!begin_tt_abort(AT_RES_SEND_FAIL, rc))
    {
        settle(AT_RES_SEND_FAIL, rc);
    }
}

/*----------------------------------------------------------------------------*
 *  Queue
 *----------------------------------------------------------------------------*/

/* Take the next queued request into flight.  Safe to call spuriously. */
static void pump(void)
{
    k_mutex_lock(&s_lock, K_FOREVER);

    if (s_step != ST_IDLE || s_q_count == 0U)
    {
        k_mutex_unlock(&s_lock);
        return;
    }

    s_flight = s_queue[s_q_head];
    s_q_head = (uint8_t)((s_q_head + 1U) % AT_CLIENT_QUEUE_DEPTH);
    s_q_count--;

    s_step        = ST_SEND_CMD;
    s_abort_done  = 0;
    s_aborting    = false;
    s_pending_res = AT_RES_TIMEOUT;
    s_pending_err = 0;

    uint32_t budget = s_flight.timeout_ms;
    bool     stream = (s_flight.kind == REQ_STREAM);

    k_mutex_unlock(&s_lock);

    /* Before the first stage, because stage_budget_arm() derives its allowance
     * from whatever patience is in force. */
    if (stream)
    {
        ready_budget_raise(AT_CLIENT_STREAM_READY_MS);
    }
    stage_budget_arm();

    /* Armed before the frame is staged, not after: a staging retry storm is
     * itself a way for a command to hang, and it must be covered. */
    (void)k_work_reschedule(&s_timeout_work, K_MSEC(budget));
    (void)k_work_reschedule(&s_stage_work, K_NO_WAIT);
}

static int enqueue(const at_req_t *req)
{
    if (!s_inited)
    {
        return -ENODEV;
    }

    k_mutex_lock(&s_lock, K_FOREVER);
    if (s_q_count >= AT_CLIENT_QUEUE_DEPTH)
    {
        k_mutex_unlock(&s_lock);
        s_stats.queue_full++;
        EBADGE_WARN("at: queue full, dropped \"%s\"", req->cmd);
        return -ENOSPC;
    }

    uint8_t tail = (uint8_t)((s_q_head + s_q_count) % AT_CLIENT_QUEUE_DEPTH);

    s_queue[tail] = *req;
    s_q_count++;
    k_mutex_unlock(&s_lock);

    s_stats.submitted++;
    pump();
    return 0;
}

/*----------------------------------------------------------------------------*
 *  Timeout
 *----------------------------------------------------------------------------*/
static void timeout_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    /* Do not time out a command while a 16 KiB socket payload is streaming in.
     * The two are unrelated -- the burst is inbound file data, the command is
     * ours -- but the burst monopolises the link for as long as it takes, and
     * failing a command that is merely waiting its turn would tear down a
     * session over a busy wire. */
    if (at_parser_in_data(&s_parser))
    {
        (void)k_work_reschedule(&s_timeout_work, K_MSEC(100));
        return;
    }

    /* A transparent-mode request that timed out before its payload was fully out
     * has left the 8711 in transparent mode, where the next ordinary command would
     * be read as socket payload.  The three '<' frames are the only way back, and
     * giving up without them would poison every command that follows.
     *
     * Applies to a stalled stream exactly as it does to a raw send, and it matters
     * more there: a stream stops mid-image with the 8711 still counting down from
     * a megabyte, so without the abort it stays in transparent mode indefinitely.
     *
     * Once they are staged, stage_advance() re-arms this handler for
     * TT_ABORT_GRACE_MS and we come back through here -- with s_aborting set, so
     * begin_tt_abort() declines and the result recorded there is reported. */
    if (begin_tt_abort(AT_RES_TIMEOUT, 0))
    {
        EBADGE_WARN("at: \"%s\" timed out mid-payload, aborting transparent mode",
                    s_flight.cmd);
        return;
    }

    settle(s_pending_res, s_pending_err);
}

/*----------------------------------------------------------------------------*
 *  Inbound classification
 *----------------------------------------------------------------------------*/

static void fan_out_event(const char *line, uint16_t len)
{
    s_stats.events++;

    for (uint8_t i = 0; i < AT_CLIENT_EVENT_SINKS; i++)
    {
        if (s_events[i].cb != NULL)
        {
            s_events[i].cb(line, len, s_events[i].user);
        }
    }
}

static void on_line(const char *line, uint16_t len, void *user)
{
    ARG_UNUSED(user);

    if (s_log_lines && len > 0U)
    {
        EBADGE_LOG(EB_DIR_FROM_8711 "at< %s", line);
    }

    /* The 8711 rebooted.  Reported before anything else, because every other
     * interpretation of the lines that follow is wrong: a command in flight is
     * gone, no socket exists any more, and the AP is down. */
    if (len >= (sizeof(LINE_READY) - 1U) &&
        strncmp(line, LINE_READY, sizeof(LINE_READY) - 1U) == 0)
    {
        s_stats.resets++;
        s_link_ready = true;

        for (uint8_t i = 0; i < AT_CLIENT_READY_SINKS; i++)
        {
            if (s_readys[i].cb != NULL)
            {
                s_readys[i].cb(s_readys[i].user);
            }
        }
        settle(AT_RES_RESET, 0);
        return;
    }

    /* Unsolicited indications may land between two lines of a command's own
     * response, so they are routed away from the command rather than counted as
     * body -- see at_parser.h. */
    if (at_parser_line_is_event(line, len))
    {
        fan_out_event(line, len);
        return;
    }

    if (len == 0U)
    {
        return;     /* blank separator line, carries nothing */
    }

    uint8_t step;

    k_mutex_lock(&s_lock, K_FOREVER);
    step = s_step;
    k_mutex_unlock(&s_lock);

    if (step == ST_IDLE)
    {
        s_stats.orphan_lines++;
        return;
    }

    if (strcmp(line, LINE_OK) == 0)
    {
        /* An "OK" that merely acknowledges the abort is not a success: the
         * payload never made it onto the socket.  Report what actually happened,
         * which is whatever drove us into the abort -- a timeout, or a chunk the
         * transport dropped. */
        settle(s_aborting ? s_pending_res : AT_RES_OK,
               s_aborting ? s_pending_err : 0);
        return;
    }
    if (strncmp(line, LINE_ERROR, sizeof(LINE_ERROR) - 1U) == 0)
    {
        settle(s_aborting ? s_pending_res : AT_RES_ERROR,
               s_aborting ? s_pending_err : parse_error_no(line, len));
        return;
    }

    if (step == ST_WAIT_PROMPT && strcmp(line, LINE_PROMPT) == 0)
    {
        k_mutex_lock(&s_lock, K_FOREVER);
        if (s_step == ST_WAIT_PROMPT)
        {
            s_step = ST_SEND_PAYLOAD;
            stage_budget_arm();
        }
        k_mutex_unlock(&s_lock);

        (void)k_work_reschedule(&s_stage_work, K_NO_WAIT);
        return;
    }

    /* Response body.  Read the sink out under the lock in case a concurrent
     * settle() is retiring the request. */
    at_line_cb_t sink;
    void        *sink_user;

    k_mutex_lock(&s_lock, K_FOREVER);
    sink      = (s_step == ST_IDLE) ? NULL : s_flight.on_line;
    sink_user = s_flight.user;
    k_mutex_unlock(&s_lock);

    if (sink != NULL)
    {
        sink(line, len, sink_user);
    }
}

static void on_data(uint8_t link_id, const uint8_t *chunk, uint16_t len,
                    uint32_t offset, uint32_t total, bool is_last, void *user)
{
    ARG_UNUSED(user);

    if (s_data_cb != NULL)
    {
        s_data_cb(link_id, chunk, len, offset, total, is_last, s_data_user);
    }
}

/* Transport RX callback.  Runs on the transport thread with M2S still asserted,
 * so everything downstream of here back-pressures the 8711 while it runs. */
static void rx_slot(const uint8_t *payload, uint16_t len)
{
    at_parser_feed(&s_parser, payload, len);
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
int at_client_init(void)
{
    if (s_inited)
    {
        return 0;
    }

    int rc = spi_at_master_init();

    if (rc != 0)
    {
        return rc;
    }

    k_mutex_init(&s_lock);
    k_work_init_delayable(&s_timeout_work, timeout_handler);
    k_work_init_delayable(&s_stage_work, stage_work_handler);

    static const at_parser_cbs_t cbs =
    {
        .on_line = on_line,
        .on_data = on_data,
        .user    = NULL,
    };

    at_parser_init(&s_parser, &cbs);

    s_q_head  = 0;
    s_q_count = 0;
    s_step    = ST_IDLE;
    memset(&s_stats, 0, sizeof(s_stats));

    spi_at_master_set_rx_cb(rx_slot);
    s_inited = true;

    EBADGE_LOG("at: client up (queue %u, cmd max %u B)",
               (unsigned)AT_CLIENT_QUEUE_DEPTH, (unsigned)AT_CLIENT_CMD_MAX);
    return 0;
}

bool at_client_link_ready(void)
{
    return s_link_ready;
}

bool at_client_busy(void)
{
    return s_step != ST_IDLE;
}

void at_client_quiesce(void)
{
    static struct k_work_sync sync;

    /* Sync form: the async cancel returns while a handler may still be running,
     * and both handlers reschedule themselves -- timeout_handler() to drive the
     * next queued command, stage_work_handler() to re-stage a refused line -- so
     * an async cancel can lose the race and leave a fresh timeout armed. */
    (void)k_work_cancel_delayable_sync(&s_timeout_work, &sync);
    (void)k_work_cancel_delayable_sync(&s_stage_work, &sync);

    /* Empty the queue and park the state machine so nothing re-arms the timers
     * if a late rx_slot()/on_line() still lands before the pads go down. */
    k_mutex_lock(&s_lock, K_FOREVER);
    s_q_head     = 0;
    s_q_count    = 0;
    s_step       = ST_IDLE;
    s_link_ready = false;
    k_mutex_unlock(&s_lock);
}

int at_client_submit(const char *cmd, at_line_cb_t on_line_cb,
                     at_done_cb_t done, void *user, uint32_t timeout_ms)
{
    if (cmd == NULL || cmd[0] == '\0')
    {
        return -EINVAL;
    }

    size_t n = strlen(cmd);

    /* -2 for the CRLF stage_current() appends in place. */
    if (n + 2U > AT_CLIENT_CMD_MAX)
    {
        return -EMSGSIZE;
    }

    at_req_t req;

    memset(&req, 0, sizeof(req));
    req.kind       = REQ_CMD;
    memcpy(req.cmd, cmd, n);
    req.on_line    = on_line_cb;
    req.done       = done;
    req.user       = user;
    req.timeout_ms = (timeout_ms != 0U) ? timeout_ms : AT_CLIENT_TIMEOUT_MS;

    return enqueue(&req);
}

int at_client_submitf(at_line_cb_t on_line_cb, at_done_cb_t done, void *user,
                      uint32_t timeout_ms, const char *fmt, ...)
{
    at_req_t req;

    memset(&req, 0, sizeof(req));
    req.kind = REQ_CMD;

    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(req.cmd, sizeof(req.cmd) - 2U, fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        return -EINVAL;
    }
    if ((size_t)n >= sizeof(req.cmd) - 2U)
    {
        /* A truncated command is not a shorter command -- a clipped
         * AT+SKTSERVER binds a different port -- so this is an error, never a
         * best effort. */
        EBADGE_ERR1("at: command truncated at %u B", (unsigned)n);
        return -EMSGSIZE;
    }

    req.on_line    = on_line_cb;
    req.done       = done;
    req.user       = user;
    req.timeout_ms = (timeout_ms != 0U) ? timeout_ms : AT_CLIENT_TIMEOUT_MS;

    return enqueue(&req);
}

int at_client_send_raw(uint8_t link, const uint8_t *data, uint16_t len,
                       at_done_cb_t done, void *user)
{
    if (data == NULL || len == 0U)
    {
        return -EINVAL;
    }
    if (len > AT_CLIENT_RAW_MAX)
    {
        return -EMSGSIZE;
    }

    at_req_t req;

    memset(&req, 0, sizeof(req));
    req.kind = REQ_RAW;
    (void)snprintf(req.cmd, sizeof(req.cmd) - 2U, "AT+SKTSENDRAW=%u,%u",
                   (unsigned)link, (unsigned)len);
    memcpy(req.raw, data, len);
    req.raw_len    = len;
    req.done       = done;
    req.user       = user;
    req.timeout_ms = AT_CLIENT_TT_TIMEOUT_MS;

    return enqueue(&req);
}

/* A per-chunk budget shorter than the transport's own ready timeout would report
 * a stall the transport has not given up on yet, and the abort frames that follow
 * would race the payload frame still sitting in the staging slot.  Compared
 * against the raised budget a stream installs, not the transport's default. */
BUILD_ASSERT(AT_CLIENT_STREAM_CHUNK_MS > AT_CLIENT_STREAM_READY_MS,
             "stream chunk budget must outlast the transport's ready timeout");

int at_client_submit_stream(const char *cmd, const at_stream_req_t *req)
{
    if (cmd == NULL || cmd[0] == '\0' || req == NULL || req->data == NULL ||
        req->len == 0U)
    {
        return -EINVAL;
    }

    size_t n = strlen(cmd);

    /* -2 for the CRLF stage_current() appends in place. */
    if (n + 2U > AT_CLIENT_CMD_MAX)
    {
        return -EMSGSIZE;
    }

    at_req_t r;

    memset(&r, 0, sizeof(r));
    r.kind = REQ_STREAM;
    memcpy(r.cmd, cmd, n);

    /* Borrowed, not copied -- see at_stream_req_t.  This is the entire reason the
     * kind exists: req.raw is 64 B and the images going through here are megabytes,
     * so the payload stays where the caller put it (PSRAM) and only 16 KiB at a
     * time is ever handed to the transport. */
    r.ext         = req->data;
    r.ext_len     = req->len;
    r.on_progress = req->on_progress;
    r.done        = req->done;
    r.user        = req->user;

    /* Three budgets rather than one because the three waits differ by orders of
     * magnitude: AT+OTAUSER allocates and initialises OTA before it answers ">>>",
     * each chunk is one slot turnaround, and the final "OK" comes only after the
     * peer has written the whole image to its flash. */
    r.timeout_ms = (req->prompt_ms != 0U) ? req->prompt_ms
                   : AT_CLIENT_STREAM_PROMPT_MS;
    r.chunk_ms   = (req->chunk_ms != 0U) ? req->chunk_ms
                   : AT_CLIENT_STREAM_CHUNK_MS;
    r.term_ms    = (req->term_ms != 0U) ? req->term_ms
                   : AT_CLIENT_STREAM_TERM_MS;

    EBADGE_LOG("at: stream \"%s\" %u B in %u chunks", r.cmd, (unsigned)r.ext_len,
               (unsigned)((r.ext_len + SPI_AT_PAYLOAD_MAX - 1U) /
                          SPI_AT_PAYLOAD_MAX));
    return enqueue(&r);
}

void at_client_add_event_cb(at_event_cb_t cb, void *user)
{
    if (cb == NULL)
    {
        return;
    }

    for (uint8_t i = 0; i < AT_CLIENT_EVENT_SINKS; i++)
    {
        if (s_events[i].cb == cb)
        {
            return;
        }
    }
    for (uint8_t i = 0; i < AT_CLIENT_EVENT_SINKS; i++)
    {
        if (s_events[i].cb == NULL)
        {
            s_events[i].cb   = cb;
            s_events[i].user = user;
            return;
        }
    }

    /* Louder than a silent drop on purpose: a sink that never fires makes
     * whatever it drove stop working with no other symptom. */
    EBADGE_ERR1("at: event sinks full (%u), sink dropped",
                (unsigned)AT_CLIENT_EVENT_SINKS);
}

void at_client_set_data_cb(at_data_cb_t cb, void *user)
{
    s_data_cb   = cb;
    s_data_user = user;
}

void at_client_add_ready_cb(at_ready_cb_t cb, void *user)
{
    if (cb == NULL)
    {
        return;
    }

    for (uint8_t i = 0; i < AT_CLIENT_READY_SINKS; i++)
    {
        if (s_readys[i].cb == cb)
        {
            return;
        }
    }
    for (uint8_t i = 0; i < AT_CLIENT_READY_SINKS; i++)
    {
        if (s_readys[i].cb == NULL)
        {
            s_readys[i].cb   = cb;
            s_readys[i].user = user;
            return;
        }
    }

    /* Same reasoning as the event sinks: a reboot sink that never fires leaves
     * its owner believing in state the 8711 has just thrown away. */
    EBADGE_ERR1("at: ready sinks full (%u), sink dropped",
                (unsigned)AT_CLIENT_READY_SINKS);
}

void at_client_set_log_lines(bool on)
{
    s_log_lines = on;
}

bool at_client_get_log_lines(void)
{
    return s_log_lines;
}

void at_client_get_stats(at_client_stats_t *out)
{
    if (out != NULL)
    {
        *out = s_stats;
    }
}

void at_client_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    memset(&s_parser.stats, 0, sizeof(s_parser.stats));
}

void at_client_get_parser_stats(at_parser_stats_t *out)
{
    at_parser_get_stats(&s_parser, out);
}

#endif /* CONFIG_SPI_AT_MASTER */
