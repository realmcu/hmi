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
 * staged frame and refuses a second with -EBUSY until the 8711 raises S2M and
 * the transaction consumes it.  A plain command needs one frame and never
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
 * exactly one place here: the timeout path, to unstick a transfer that is
 * genuinely short.
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
 * only ever sent from the timeout path. */
#define TT_ABORT_BYTE   '<'
#define TT_ABORT_FRAMES 3

/* How long to keep retrying one frame when the transport is still busy.
 *
 * 5 ms because that is roughly the round trip of one 16 KiB slot at 25 MHz plus
 * the peer's turnaround, so retrying faster only burns work-queue slots.  The
 * 500 ms total is under SPI_AT_READY_TIMEOUT_MS (1000): past that point the
 * transport abandons the earlier request itself, and a retry that succeeded
 * afterwards would be putting frame N+1 on a wire that never carried frame N. */
#define STAGE_RETRY_MS      5U
#define STAGE_RETRY_MAX     100U

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
enum { REQ_CMD = 0, REQ_RAW };

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
} at_req_t;

/* In-flight progress.  A plain command only ever sees SEND_CMD -> WAIT_TERM, and
 * a raw one SEND_CMD -> WAIT_PROMPT -> SEND_PAYLOAD -> WAIT_TERM.  SEND_ABORT is
 * off the normal path entirely: only the timeout handler enters it. */
enum
{
    ST_IDLE = 0,
    ST_SEND_CMD,      /* staging the command line                             */
    ST_WAIT_PROMPT,   /* raw only: waiting for ">>>"                          */
    ST_SEND_PAYLOAD,  /* raw only: staging the payload frame                  */
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
static bool      s_aborting;    /* the timeout path is unsticking transparent   */
static uint32_t  s_stage_tries;

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

/* Advance past a successfully staged frame. */
static void stage_advance(void)
{
    k_mutex_lock(&s_lock, K_FOREVER);

    switch (s_step)
    {
    case ST_SEND_CMD:
        s_step = (s_flight.kind == REQ_RAW) ? ST_WAIT_PROMPT : ST_WAIT_TERM;
        break;

    case ST_SEND_PAYLOAD:
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

    bool more    = (s_step == ST_SEND_ABORT);
    bool aborted = s_aborting && (s_step == ST_WAIT_TERM);

    s_stage_tries = 0;
    k_mutex_unlock(&s_lock);

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

    if (rc == -EBUSY && s_stage_tries < STAGE_RETRY_MAX)
    {
        s_stage_tries++;
        (void)k_work_reschedule(&s_stage_work, K_MSEC(STAGE_RETRY_MS));
        return;
    }

    EBADGE_ERR("at: cannot stage step %u (%d) -- 8711 not answering S2M?",
               (unsigned)s_step, rc);
    settle(AT_RES_SEND_FAIL, rc);
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
    s_stage_tries = 0;

    uint32_t budget = s_flight.timeout_ms;

    k_mutex_unlock(&s_lock);

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

    /* A raw request that timed out before its payload was fully out has left the
     * 8711 in transparent mode, where the next ordinary command would be read as
     * socket payload.  The three '<' frames are the only way back -- so this is
     * the one place they are sent.  Giving up without them would poison every
     * command that follows.
     *
     * Once they are staged, stage_advance() re-arms this handler for
     * TT_ABORT_GRACE_MS and we come back through here to report the timeout. */
    bool need_abort = false;

    k_mutex_lock(&s_lock, K_FOREVER);
    if (!s_aborting && s_flight.kind == REQ_RAW &&
        (s_step == ST_WAIT_PROMPT || s_step == ST_SEND_PAYLOAD))
    {
        s_aborting    = true;
        s_abort_done  = 0;
        s_stage_tries = 0;
        s_step        = ST_SEND_ABORT;
        need_abort    = true;
    }
    k_mutex_unlock(&s_lock);

    if (need_abort)
    {
        EBADGE_WARN("at: raw timed out mid-payload, aborting transparent mode");
        (void)k_work_reschedule(&s_stage_work, K_NO_WAIT);
        return;
    }

    settle(AT_RES_TIMEOUT, 0);
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
         * payload never made it onto the socket.  Report what actually
         * happened. */
        settle(s_aborting ? AT_RES_TIMEOUT : AT_RES_OK, 0);
        return;
    }
    if (strncmp(line, LINE_ERROR, sizeof(LINE_ERROR) - 1U) == 0)
    {
        settle(s_aborting ? AT_RES_TIMEOUT : AT_RES_ERROR,
               s_aborting ? 0 : parse_error_no(line, len));
        return;
    }

    if (step == ST_WAIT_PROMPT && strcmp(line, LINE_PROMPT) == 0)
    {
        k_mutex_lock(&s_lock, K_FOREVER);
        if (s_step == ST_WAIT_PROMPT)
        {
            s_step        = ST_SEND_PAYLOAD;
            s_stage_tries = 0;
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
