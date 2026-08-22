/**
 * @file    wifi_8711_at.c
 * @brief   ATMC transaction layer -- see the header for the layering rationale.
 *
 * CONCURRENCY
 * -----------
 * Three contexts touch the state below:
 *
 *   - callers of wifi_8711_at_submit(), typically the l2_task
 *   - the transport thread, through at_slot_sink()
 *   - the system workqueue, through the timeout handler
 *
 * so the pending-transaction fields are guarded by a mutex.  The window that
 * actually matters is small but real: a RESPONSE can land on the transport
 * thread at the same moment the timeout fires on the workqueue, and without the
 * lock both would run the completion.  The lock is therefore released BEFORE
 * the user callback is invoked -- the callback may call straight back into
 * wifi_8711_at_submit() to chain a second command, which would deadlock on a
 * non-recursive mutex.
 *
 * The claim/complete pattern used throughout is: take the lock, atomically
 * detach the pending transaction into locals, drop the lock, then run the
 * callback from the locals.  Whoever detaches it first owns the completion, so
 * a late RESPONSE finds nothing to complete and is counted as stale instead.
 */
#include <zephyr/kernel.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include "wifi_8711.h"
#include "wifi_8711_xfer.h"
#include "wifi_8711_at.h"
#include "spi_at_protocol.h"
#include "../protocol/ebadge_log.h"

#if defined(CONFIG_WIFI_8711)

/*----------------------------------------------------------------------------*
 *  State
 *----------------------------------------------------------------------------*/
static K_MUTEX_DEFINE(s_lock);

/* Sequence of the outstanding command, or 0 when idle.  The protocol reserves
 * Sequence 0 for POLL (sec.6), so 0 is unambiguous as "nothing pending". */
static uint32_t             s_pending_seq;
static wifi_8711_at_cb_t    s_pending_cb;
static void                *s_pending_user;

static wifi_8711_jpg_sink_t s_jpg_sink;
static wifi_8711_file_sink_t s_file_sink;
static wifi_8711_at_stats_t s_stats;

/* Timeout is a delayed work item rather than a thread: it fires at most once
 * per transaction and the handler only needs to log and complete, so a whole
 * thread would be a stack for nothing. */
static void at_timeout_work(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_timeout_work, at_timeout_work);

/*----------------------------------------------------------------------------*
 *  Completion -- the one place a pending transaction is retired
 *----------------------------------------------------------------------------*/

/**
 * Detach the pending transaction under the lock and return it in the out
 * params.  Returns false if there was nothing pending, which is the normal
 * outcome for the loser of a RESPONSE/timeout race.
 *
 * @param  want_seq  only claim if this sequence matches; 0 claims whatever is
 *                   pending (used by abort and by the timeout, which has no
 *                   sequence of its own to match).
 */
static bool at_claim(uint32_t want_seq, wifi_8711_at_cb_t *out_cb,
                     void **out_user)
{
    bool claimed = false;

    k_mutex_lock(&s_lock, K_FOREVER);
    if (s_pending_seq != 0U && (want_seq == 0U || want_seq == s_pending_seq))
    {
        *out_cb        = s_pending_cb;
        *out_user      = s_pending_user;
        s_pending_seq  = 0U;
        s_pending_cb   = NULL;
        s_pending_user = NULL;
        claimed        = true;
    }
    k_mutex_unlock(&s_lock);
    return claimed;
}

/**
 * Retire a claimed transaction: cancel the timeout, restore idle TX content,
 * then invoke the callback.
 *
 * Order matters.  The TX reset comes first because sec.4.2 requires idle
 * content once a command has been dealt with -- without it the answered
 * COMMAND stays on MISO for every subsequent slot.  The 8711 de-duplicates on
 * Sequence (sec.11.3) so it does no harm on the wire, but the link would
 * permanently advertise a command that is already finished, and nothing could
 * tell "waiting for a reply" from "done".
 *
 * The callback runs last and with no lock held, so it may submit the next
 * command from inside itself.
 */
static void at_finish(wifi_8711_at_cb_t cb, void *user,
                      wifi_8711_at_result_t res, const char *text, size_t len)
{
    /* Cancel rather than just let it fire: the work item is shared across
     * transactions, so a stale timeout left armed would land on whatever
     * command comes next. */
    (void)k_work_cancel_delayable(&s_timeout_work);

    (void)wifi_8711_xfer_set_tx_idle();

    if (cb != NULL)
    {
        cb(res, (text != NULL) ? text : "", len, user);
    }
}

/*----------------------------------------------------------------------------*
 *  Timeout
 *----------------------------------------------------------------------------*/
static void at_timeout_work(struct k_work *work)
{
    ARG_UNUSED(work);

    wifi_8711_at_cb_t cb   = NULL;
    void             *user = NULL;

    /* want_seq 0: claim whatever is pending.  A RESPONSE that arrives while
     * this work item is already queued will have claimed it first, in which
     * case there is nothing to time out and we stay quiet. */
    if (!at_claim(0U, &cb, &user))
    {
        return;
    }

    s_stats.timeouts++;
    /* Report the POLL count: it separates "the link is dead" from "the link is
     * alive but the 8711 is not answering this command", which need completely
     * different investigations. */
    EBADGE_WARN2("wifi8711 at: command timed out after %u ms (polls seen=%u)",
                 (unsigned)WIFI_8711_AT_TIMEOUT_MS, (unsigned)s_stats.polls);

    at_finish(cb, user, WIFI_8711_AT_ERR_TIMEOUT, NULL, 0U);
}

/*----------------------------------------------------------------------------*
 *  Slot sink -- transport-thread context
 *
 *  Runs with B2W low for its whole duration, which back-pressures the 8711, so
 *  it must return promptly.  A few log lines are acceptable; blocking is not.
 *----------------------------------------------------------------------------*/
static void at_slot_sink(const uint8_t *rx, size_t len)
{
    if (rx == NULL || len < WIFI_8711_HEADER_SIZE)
    {
        return;
    }

    uint32_t magic = spi_at_get_le32(rx);

    if (magic == WIFI_8711_JPG_MAGIC)
    {
        s_stats.jpg_slots++;
        if (s_jpg_sink != NULL)
        {
            s_jpg_sink(rx, len);
        }
        return;
    }

    if (magic == WIFI_8711_FILE_MAGIC)
    {
        /* A file chunk (protocol sec.6).  Note that these are NOT guaranteed to
         * arrive as an unbroken run: sec.4.1 item 3 lets the 8711 interleave ATMC
         * slots between chunks, which is what makes it possible to get an
         * AT+XFERSTOP through mid-upload.  Nothing here or downstream may assume
         * START..END is contiguous on the wire. */
        s_stats.file_slots++;
        if (s_file_sink != NULL)
        {
            s_file_sink(rx, len);
        }
        return;
    }

    if (magic != WIFI_8711_AT_MAGIC)
    {
        /* Includes the all-zero slot, which is the normal state of a link whose
         * MOSI is not wired or whose peer firmware is not running.  Dump the
         * head raw: this is the "the link moves bytes but we disagree about the
         * format" case, and a decoded view would only hide it.
         *
         * Logged only once per 64 such slots -- on a miswired link EVERY slot
         * looks like this, and at the POLL rate that would bury everything
         * else in the log. */
        s_stats.bad_magic++;
        if ((s_stats.bad_magic % 64U) == 1U)
        {
            EBADGE_LOG2("wifi8711 at: unknown magic %08x (x%u), head follows",
                        (unsigned)magic, (unsigned)s_stats.bad_magic);
            EBADGE_LOG_HEX("wifi8711 rx", rx, 32);
        }
        return;
    }

    spi_at_packet_t pkt;
    spi_at_status_t st = spi_at_parse_packet(rx, &pkt);
    if (st != SPI_AT_OK)
    {
        /* CRC is called out separately: it is the difference between "the link
         * is broken" and "the link works but the bytes got corrupted". */
        s_stats.parse_err++;
        EBADGE_WARN2("wifi8711 at: ATMC parse failed %d%s", (int)st,
                     (st == SPI_AT_ERR_CRC) ? " (CRC -- bit errors on the bus)"
                     : "");
        return;
    }

    if (pkt.type == SPI_AT_TYPE_POLL)
    {
        /* Heartbeat.  Counted always; logged only while a command is outstanding.
         *
         * Unconditionally would be one line every few seconds for the life of the
         * device, which pushes the reply we are waiting for out of the scroll
         * buffer.  While waiting, though, each POLL is the useful datum: it says
         * the link is alive and the 8711 simply has not answered yet, which is a
         * completely different fault from silence.  Bounded by the deadline, so
         * the burst cannot outlast one transaction. */
        s_stats.polls++;
        if (s_pending_seq != 0U)
        {
            EBADGE_LOG(EB_DIR_FROM_8711 "POLL #%u (waiting on seq=%u)",
                       (unsigned)s_stats.polls, (unsigned)s_pending_seq);
        }
        return;
    }

    if (pkt.type != SPI_AT_TYPE_RESPONSE)
    {
        /* A COMMAND from the 8711 would land here.  Nothing in its firmware
         * sends one, so seeing it means the two ends disagree about direction
         * -- worth a line rather than a silent drop. */
        EBADGE_LOG1("wifi8711 at: unexpected ATMC type %u", (unsigned)pkt.type);
        return;
    }

    /* Copy the text out to get a NUL terminator.  The payload is only
     * effectively terminated by the slot's zero padding, so handing the raw
     * pointer to a callback that used it as a string would run off into the
     * rest of the slot. */
    char   text[WIFI_8711_AT_TEXT_MAX];
    size_t n = spi_at_copy_payload(&pkt, text, sizeof(text));

    /* Dump every RESPONSE body verbatim, HERE, before sequence matching decides
     * whether anyone still wants it.
     *
     * Deliberately ahead of the claim: the replies most worth seeing are the ones
     * that get dropped.  A sequence mismatch or a lost race with the timeout
     * discards a reply that the 8711 did in fact send, and with the dump further
     * downstream that reply left no trace of its contents at all -- the log said
     * "stale, dropped" and the bytes were gone.  That is how an 8 s deadline
     * against a 12 s link looked like a dead radio for far longer than it should
     * have.
     *
     * Cheap enough for this context: a couple of printf lines with B2W held low,
     * which back-pressures the 8711 but does not block. */
    EBADGE_LOG(EB_DIR_FROM_8711 "RESPONSE seq=%u, %u B:", (unsigned)pkt.sequence,
               (unsigned)n);
    ebadge_log_lines(EB_DIR_FROM_8711 "  ", text);

    wifi_8711_at_cb_t cb   = NULL;
    void             *user = NULL;

    if (!at_claim(pkt.sequence, &cb, &user))
    {
        /* Either the command already timed out, or this is a reply to an older
         * sequence.  Both are "stale": completing the current transaction with
         * someone else's answer would be worse than dropping it. */
        s_stats.stale++;
        EBADGE_WARN2("wifi8711 at: stale RESPONSE seq=%u (pending=%u), dropped",
                     (unsigned)pkt.sequence, (unsigned)s_pending_seq);
        return;
    }

    s_stats.responses++;

    /* sec.9.3: any command the 8711 does not recognise comes back as
     * "[AT]:ERROR".  Map it to a distinct result so callers do not have to
     * string-match, and so a wrong command is never mistaken for a valid
     * reply that simply failed to parse. */
    wifi_8711_at_result_t res = WIFI_8711_AT_OK;
    if (strstr(text, "[AT]:ERROR") != NULL)
    {
        res = WIFI_8711_AT_ERR_PEER;
        EBADGE_WARN("wifi8711 at: peer answered [AT]:ERROR (unknown command,"
                    " or AP not running for WLSTATE)");
    }
    else if (n < pkt.length)
    {
        /* Flagged rather than silently accepted: a caller parsing a truncated
         * WLSTATE reply would see a short CLIENT list and believe it. */
        res = WIFI_8711_AT_ERR_TRUNC;
        EBADGE_WARN2("wifi8711 at: response truncated, %u of %u B",
                     (unsigned)n, (unsigned)pkt.length);
    }

    at_finish(cb, user, res, text, n);
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
void wifi_8711_at_set_jpg_sink(wifi_8711_jpg_sink_t cb)
{
    s_jpg_sink = cb;
}

void wifi_8711_at_set_file_sink(wifi_8711_file_sink_t cb)
{
    s_file_sink = cb;
}

int wifi_8711_at_submit(const char *cmd, wifi_8711_at_cb_t cb, void *user)
{
    uint32_t seq = 0U;
    int      rc;

    if (cmd == NULL)
    {
        return -EINVAL;
    }

    if (!wifi_8711_ready())
    {
        /* wifi_8711_init() failed or was never built in.  Non-fatal at boot by
         * design (main.c), so it is entirely possible to get here. */
        EBADGE_WARN("wifi8711 at: link not initialised (wifi_8711_init failed?)");
        return -ENODEV;
    }

    /* Claim the single-flight slot before touching the wire, so two callers
     * racing here cannot both stage into the one TX buffer. */
    k_mutex_lock(&s_lock, K_FOREVER);
    if (s_pending_seq != 0U)
    {
        k_mutex_unlock(&s_lock);
        EBADGE_WARN1("wifi8711 at: busy (seq=%u outstanding), refusing submit",
                     (unsigned)s_pending_seq);
        return -EBUSY;
    }
    /* Reserve the slot with a placeholder so a concurrent submit sees "busy"
     * while we are still staging.  Overwritten with the real sequence below;
     * any non-zero value works, and 0 must be avoided because it reads as
     * idle. */
    s_pending_seq  = UINT32_MAX;
    s_pending_cb   = cb;
    s_pending_user = user;
    k_mutex_unlock(&s_lock);

    /* Stage BEFORE starting the transport, not after.  Staged content is only
     * promoted into the DMA buffer at an ARM, so on a not-yet-started transport
     * this ordering puts the command into the very first armed slot; the other
     * way round the first slot goes out all-zero and the command waits for the
     * next ARM -- a whole POLL period, up to ~2 s wasted for nothing. */
    rc = wifi_8711_xfer_test_at(cmd, &seq);
    if (rc != 0)
    {
        EBADGE_ERR1("wifi8711 at: staging failed %d", rc);
        goto fail_release;
    }

    /* Publish the real sequence now that we have it, so an incoming RESPONSE
     * can be matched.  A reply cannot possibly have arrived yet -- the command
     * has not been clocked out. */
    k_mutex_lock(&s_lock, K_FOREVER);
    s_pending_seq = seq;
    k_mutex_unlock(&s_lock);

    /* Nothing in main() starts the transport, so the first submit has to.
     * Without this the command would stage into a transport that never clocks
     * it and we would report success for a byte that never left. */
    if (!wifi_8711_xfer_started())
    {
        rc = wifi_8711_xfer_start(at_slot_sink);
        if (rc != 0)
        {
            EBADGE_ERR1("wifi8711 at: xfer_start failed %d", rc);
            goto fail_release;
        }
        EBADGE_LOG1("wifi8711 at: transport started on demand, b2w=%d",
                    wifi_8711_get_ready());
    }
    else
    {
        /* Already running -- most likely started from the shell with a NULL
         * sink, in which case the reply would go nowhere without this. */
        (void)wifi_8711_xfer_set_sink(at_slot_sink);
    }

    /* Arm the deadline only once the command is genuinely on its way.  Arming
     * it earlier would let a staging failure leave a timeout running against a
     * transaction that no longer exists. */
    (void)k_work_schedule(&s_timeout_work, K_MSEC(WIFI_8711_AT_TIMEOUT_MS));

    s_stats.submits++;
    /* Spell the timing out, because "it returned 0 but nothing happened yet" is
     * the expected state for the next couple of seconds, not a failure.
     *
     * The command text is printed verbatim and tagged with its direction, so the
     * console shows the exact bytes staged for the 8711 next to the reply they
     * eventually produce -- the pair is what makes a vendor-side rename or an
     * argument we got wrong visible, instead of just "no usable AP state". */
    EBADGE_LOG(EB_DIR_TO_8711 "AT command seq=%u: %s", (unsigned)seq, cmd);
    EBADGE_LOG(EB_DIR_TO_8711 "8711 owns the clock -- reply in ~2-4 s when idle,"
               " deadline %u ms", (unsigned)WIFI_8711_AT_TIMEOUT_MS);
    return 0;

fail_release:
    /* Release the single-flight slot, otherwise one failed submit would wedge
     * the layer at -EBUSY forever with no transaction to time out and clear
     * it.  No callback: the caller is getting the error as a return value and
     * would otherwise be told twice. */
    k_mutex_lock(&s_lock, K_FOREVER);
    s_pending_seq  = 0U;
    s_pending_cb   = NULL;
    s_pending_user = NULL;
    k_mutex_unlock(&s_lock);
    return rc;
}

bool wifi_8711_at_busy(void)
{
    bool busy;

    k_mutex_lock(&s_lock, K_FOREVER);
    busy = (s_pending_seq != 0U);
    k_mutex_unlock(&s_lock);
    return busy;
}

void wifi_8711_at_abort(void)
{
    wifi_8711_at_cb_t cb   = NULL;
    void             *user = NULL;

    if (!at_claim(0U, &cb, &user))
    {
        return;
    }
    EBADGE_LOG("wifi8711 at: transaction aborted");
    at_finish(cb, user, WIFI_8711_AT_ERR_ABORT, NULL, 0U);
}

void wifi_8711_at_get_stats(wifi_8711_at_stats_t *out)
{
    if (out != NULL)
    {
        *out = s_stats;
    }
}

void wifi_8711_at_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}

const char *wifi_8711_at_result_str(wifi_8711_at_result_t res)
{
    switch (res)
    {
    case WIFI_8711_AT_OK:          return "OK";
    case WIFI_8711_AT_ERR_TIMEOUT: return "TIMEOUT";
    case WIFI_8711_AT_ERR_PEER:    return "PEER-ERROR";
    case WIFI_8711_AT_ERR_TRUNC:   return "TRUNCATED";
    case WIFI_8711_AT_ERR_ABORT:   return "ABORTED";
    default:                       return "?";
    }
}

#endif /* CONFIG_WIFI_8711 */
