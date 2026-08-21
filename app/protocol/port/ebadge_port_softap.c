/**
 * @file    ebadge_port_softap.c
 * @brief   SoftAP port backed by the RTL8711FA over SPI.
 *
 * Two jobs, and they are not the same job:
 *
 *   1. Keep the AP credentials available synchronously, because the BLE
 *      handlers that need them run on l2_task and a query costs 2..4 s.
 *   2. Turn "a station associated" into a callback, which the 8711 does not
 *      offer as an event -- so it has to be polled.
 *
 * ---------------------------------------------------------------------------
 * WHY STA-JOINED IS A POLL AND NOT AN INTERRUPT
 * ---------------------------------------------------------------------------
 * The only association signal the 8711 exposes is the CLIENTS= line in an
 * AT+WLSTATE reply.  There is no unsolicited notification: the AT channel only
 * ever answers a command, and B2W is a readiness gate rather than a request
 * line, so the 8711 has no way to tell us anything we did not ask for.
 *
 * So while a session is waiting for the phone, this module asks -- every
 * SOFTAP_JOIN_POLL_MS.  The rate is a compromise: the sessions allow 60 s for
 * the STA to join, and each query occupies the single-flight AT layer for a
 * couple of seconds, so polling much faster would just mean the layer is never
 * free for anything else, while polling much slower would add its own period to
 * the phone's wait before the transfer starts.
 *
 * The poll deliberately stops the moment a client is seen.  It is only there to
 * find the edge; once found, a running transfer is evidence enough.
 */
#include <errno.h>
#include <string.h>

#include "ebadge_port_softap.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#if defined(CONFIG_WIFI_8711)
#include "wifi_8711_at_ap.h"
#endif

/** How often to ask "has anyone joined?" while a session is waiting. */
#define SOFTAP_JOIN_POLL_MS      3000u

/** How often to retry priming the cache when nothing is known yet.  Slower
 *  than the join poll: this runs at boot with nobody waiting on it, and a
 *  failing link should not hold the AT layer busy in a tight loop.          */
#define SOFTAP_PRIME_POLL_MS    10000u

static bool                          s_running;
static ebadge_softap_sta_joined_cb_t s_joined_cb;
static bool                          s_joined_reported;
static uint32_t                      s_next_poll_ms;

#if defined(CONFIG_WIFI_8711)

/* Consecutive submit failures, for log rate-limiting only.  On a board with no
 * 8711 the prime poll never succeeds, and one warning every 10 s forever would
 * bury everything else in the log. */
static uint32_t                      s_submit_fails;

/*----------------------------------------------------------------------------*
 *  Query completion
 *
 *  wifi_8711_at_ap_* callbacks arrive on the transport thread or the system
 *  workqueue.  The cache inside wifi_8711_at_ap.c is mutex-guarded and safe to
 *  read from anywhere, but s_joined_cb belongs to a session on l2_task, so the
 *  edge has to be marshalled across.
 *----------------------------------------------------------------------------*/

/** Runs on l2_task: deliver the joined edge to whoever is waiting. */
static void report_joined(void *arg)
{
    (void)arg;

    /* Re-check on this side of the hop.  The session may have failed its
     * WAIT_STA deadline, or been aborted by a BLE disconnect, in the seconds
     * between the query going out and the reply coming back -- calling a
     * callback the session has already dropped would resurrect a dead
     * transfer. */
    if (!s_running || s_joined_cb == NULL || s_joined_reported)
    {
        return;
    }
    s_joined_reported = true;

    ebadge_softap_sta_joined_cb_t cb = s_joined_cb;
    EBADGE_LOG("port_softap: STA associated -> notifying session");
    cb();
}

static void ap_query_done(bool ok, const wifi_8711_ap_info_t *info, void *user)
{
    (void)user;

    if (!ok)
    {
        /* Already logged by the AP layer with the specific reason.  Nothing to
         * do: the tick will try again, and until it succeeds the cache simply
         * stays as it was -- stale credentials are still better than none,
         * because the 8711 cannot change them. */
        return;
    }

    if (info->clients > 0U)
    {
        (void)ebadge_task_post_call(report_joined, NULL);
    }
}

#endif /* CONFIG_WIFI_8711 */

/*----------------------------------------------------------------------------*
 *  Tick -- on l2_task
 *----------------------------------------------------------------------------*/
static void on_tick(uint32_t now_ms)
{
#if defined(CONFIG_WIFI_8711)
    /* Two reasons to be on the wire, with different urgencies:
     *   - a session is waiting for the phone to associate  (fast)
     *   - nothing is cached yet, so no session could even start  (slow)
     * Anything else and we stay quiet; the credentials are static. */
    bool     want_join  = (s_running && s_joined_cb != NULL && !s_joined_reported);
    bool     want_prime = !wifi_8711_at_ap_cached(NULL);

    if (!want_join && !want_prime)
    {
        return;
    }
    if ((int32_t)(now_ms - s_next_poll_ms) < 0)
    {
        return;
    }
    /* Schedule the next attempt before submitting, not after: on -EBUSY we
     * still want to back off rather than retry on every 100 ms tick. */
    s_next_poll_ms = now_ms + (want_join ? SOFTAP_JOIN_POLL_MS
                               : SOFTAP_PRIME_POLL_MS);

    int rc = wifi_8711_at_ap_query(ap_query_done, NULL);
    if (rc == 0 || rc == -EBUSY)
    {
        /* -EBUSY is routine -- the AT layer is single flight and something
         * else (a debug command, the previous poll) has it. */
        s_submit_fails = 0U;
        return;
    }
    /* Anything else means the link itself is unusable.  Worth saying, but not
     * on every retry: on a board where the 8711 never comes up this path runs
     * every 10 s for the lifetime of the device.  First failure, then every
     * 32nd (~5 min), which is enough to show it is still broken. */
    s_submit_fails++;
    if (s_submit_fails == 1U || (s_submit_fails % 32U) == 0U)
    {
        EBADGE_WARN2("port_softap: ap_query failed %d (x%u)", rc,
                     (unsigned)s_submit_fails);
    }
#else
    (void)now_ms;
#endif
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
void ebadge_port_softap_init(void)
{
    ebadge_task_set_tick(on_tick);
#if defined(CONFIG_WIFI_8711)
    /* Prime immediately rather than waiting out the first poll interval: the
     * first transfer offer can arrive as soon as the phone connects over BLE,
     * and without a cached SSID it would be answered -EAGAIN. */
    s_next_poll_ms = ebadge_task_now_ms();
    EBADGE_LOG("port_softap: 8711 backend, priming AP cache");
#else
    EBADGE_WARN("port_softap: CONFIG_WIFI_8711=n -- no radio, AP unavailable");
#endif
}

int ebadge_port_softap_start(ebadge_softap_info_t *out_info,
                             uint16_t *out_port,
                             ebadge_softap_sta_joined_cb_t joined_cb)
{
    if (out_info == NULL || out_port == NULL)
    {
        return -EINVAL;
    }

    if (!ebadge_port_softap_info(out_info, out_port))
    {
        /* Nothing known yet.  Do NOT pretend success: the session would emit a
         * 0x13 full of zeroes and the phone would try to join a network called
         * "".  -EAGAIN maps to NOT_READY on the wire, which is the truth. */
        EBADGE_WARN("port_softap: start with no AP info cached -> EAGAIN");
        return -EAGAIN;
    }

    s_joined_cb       = joined_cb;
    s_joined_reported = false;
    s_running         = true;
    /* Start the join poll on the next tick rather than after a full interval,
     * so a phone that is already associated is noticed within ~100 ms instead
     * of waiting out SOFTAP_JOIN_POLL_MS for a state we could already see. */
    s_next_poll_ms    = ebadge_task_now_ms();

#if defined(CONFIG_WIFI_8711)
    /* Confirm the AP alongside, and ignore the result: the credentials just
     * handed out came from a successful query, the 8711's AP self-starts at
     * boot, and WLSTARTAP is idempotent.  This is belt-and-braces for the case
     * where the AP went down since the cache was filled -- if it fails, the
     * session's own 60 s WAIT_STA deadline is the backstop. */
    (void)wifi_8711_at_ap_start(ap_query_done, NULL);
#endif

    EBADGE_LOG2("port_softap: session using ssid=\"%s\" port=%u",
                out_info->ssid, (unsigned)*out_port);
    return 0;
}

int ebadge_port_softap_stop(void)
{
    if (s_running)
    {
        /* Say plainly that the radio stays up, so a log reader does not go
         * looking for the AP teardown that never happens. */
        EBADGE_LOG("port_softap: session released (8711 AP stays up -- not ours)");
    }
    s_running         = false;
    s_joined_cb       = NULL;
    s_joined_reported = false;
    return 0;
}

bool ebadge_port_softap_running(void)
{
    return s_running;
}

bool ebadge_port_softap_info(ebadge_softap_info_t *out_info, uint16_t *out_port)
{
#if defined(CONFIG_WIFI_8711)
    wifi_8711_ap_info_t ap;

    if (!wifi_8711_at_ap_cached(&ap) || ap.ssid[0] == '\0')
    {
        return false;
    }

    if (out_info != NULL)
    {
        memset(out_info, 0, sizeof(*out_info));
        memcpy(out_info->ssid, ap.ssid, sizeof(out_info->ssid) - 1U);
        memcpy(out_info->password, ap.password, sizeof(out_info->password) - 1U);
        out_info->ip = ap.ip;
        /* WLSTATE does not report the channel and there is no command that
         * does.  Leaving it 0 is honest and harmless: the phone scans for the
         * SSID, and 0x13's channel TLV is a hint, not a tuning instruction. */
        out_info->channel = 0U;
    }
    if (out_port != NULL)
    {
        *out_port = ap.port;
    }
    return true;
#else
    (void)out_info; (void)out_port;
    return false;
#endif
}
