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
 * SOFTAP_JOIN_POLL_MS.  The rate is set by the AT layer's round trip, which is
 * 11..13 s on hardware and single-flight: polling faster than that does not ask
 * more often, it just collects -EBUSY and keeps the layer occupied for the whole
 * 60 s the session allows.  See the macro for what that looked like when the rate
 * was picked off the deadline instead.
 *
 * The poll deliberately stops the moment a client is seen.  It is only there to
 * find the edge; once found, a running transfer is evidence enough.
 *
 * ---------------------------------------------------------------------------
 * FILLING THE CREDENTIAL CACHE IS NOT A POLL ANY MORE
 * ---------------------------------------------------------------------------
 * It used to be, on this same tick, and it was the one query that ran with
 * nobody waiting for the answer: every SOFTAP_PRIME_POLL_MS, for as long as the
 * cache was empty, forever.  On a board whose 8711 never answers that is a query
 * every 10 s for the life of the device, each one taking the single-flight AT
 * layer and handing -EBUSY to whatever did have a caller.
 *
 * Two things replace it, neither of them periodic:
 *
 *   - a bounded burst of SOFTAP_PRIME_TRIES attempts after boot.  Bounded, but
 *     not one: main() calls ebadge_task_init() BEFORE wifi_8711_init(), so the
 *     first attempt is guaranteed to fail -ENODEV and a retry is not optional.
 *   - prime_cache_on_demand(), reached from ebadge_port_softap_info() when it
 *     misses, so the query is provoked by a caller that actually wants the
 *     credentials.  Not limited by the boot budget -- see its own comment.
 *
 * The effect is that an idle device eventually goes quiet on the AT link
 * entirely: no session waiting, and either the cache is warm or the boot
 * attempts are spent.
 */
#include <errno.h>
#include <string.h>

#include "ebadge_port_softap.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#if defined(CONFIG_WIFI_8711)
#include "wifi_8711_at_ap.h"
#endif

/** How often to ask "has anyone joined?" while a session is waiting.
 *
 *  Sized off the MEASURED round trip, not off the 60 s deadline.  This was 3000,
 *  chosen as "20 polls inside WAIT_STA", which sounds generous and was in fact
 *  useless: a query takes 11..13 s on real hardware, so ticks 2, 3 and 4 all hit
 *  a single-flight AT layer that the first query still owns and got -EBUSY.  The
 *  log filled with "busy (seq=N outstanding), refusing submit" and the layer was
 *  occupied for the entire 60 s, so nothing else -- a prime, a debug command --
 *  could get a word in.
 *
 *  15 s clears the observed worst case with a little room, which makes every
 *  submit a real question.  The cost is that the joined edge can be up to 15 s
 *  late; acceptable against a 60 s deadline, and the alternative was queries that
 *  never went out at all.                                                      */
#define SOFTAP_JOIN_POLL_MS     15000u

/** Gap between the bounded boot attempts at filling the credential cache.
 *  Slower than the join poll because nobody is waiting on these.             */
#define SOFTAP_PRIME_RETRY_MS   10000u

/** How many times to try filling the cache after boot before giving up and
 *  waiting for a caller to ask.
 *
 *  Not 1: main() runs ebadge_task_init() before wifi_8711_init(), so the first
 *  attempt always returns -ENODEV.  Not unbounded either -- that was the old
 *  behaviour, and on a board with no working 8711 it never stopped.  Three
 *  spans ~20 s from boot, comfortably past the point where the link either
 *  works or does not.                                                        */
#define SOFTAP_PRIME_TRIES          3u

static bool                          s_running;
static ebadge_softap_sta_joined_cb_t s_joined_cb;
static bool                          s_joined_reported;
static uint32_t                      s_next_poll_ms;

#if defined(CONFIG_WIFI_8711)

/* Boot attempts at filling the credential cache that are still owed.  Counts
 * down to zero and stays there; nothing rearms it, which is what makes the
 * AT link go quiet on an idle device. */
static uint8_t                       s_prime_tries;

/* Rate limit for the on-demand path, which is triggered from outside (an App
 * retrying 0x12).  s_demand_valid distinguishes "never queried on demand" from
 * a deadline that happens to compare as past. */
static uint32_t                      s_next_demand_ms;
static bool                          s_demand_valid;

/* Consecutive submit failures, for log rate-limiting only.  The attempts are
 * bounded now, so this can no longer run forever -- it still exists because
 * prime_cache_once() can be provoked by a caller repeatedly (an App that keeps
 * retrying 0x12 against a dead 8711) and a warning per attempt would bury the
 * log just the same. */
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
         * do here: a failed query leaves the cache as it was -- stale
         * credentials are still better than none, because the 8711 cannot
         * change them.  Note that a failure does NOT schedule a retry of its
         * own; the tick spends the next boot attempt if any remain, and after
         * that the next caller to miss the cache provokes one. */
        return;
    }

    if (info->clients > 0U)
    {
        (void)ebadge_task_post_call(report_joined, NULL);
    }
}

/*----------------------------------------------------------------------------*
 *  Query submission, shared by the join poll and the prime attempts
 *----------------------------------------------------------------------------*/

/** Put one AT+WLSTATE on the wire.  @return the submit rc, for the caller to
 *  decide whether an attempt was consumed. */
static int submit_query(void)
{
    int rc = wifi_8711_at_ap_query(ap_query_done, NULL);
    if (rc == 0 || rc == -EBUSY)
    {
        /* -EBUSY is routine -- the AT layer is single flight and something
         * else (a debug command, the previous poll) has it. */
        s_submit_fails = 0U;
        return rc;
    }
    /* Anything else means the link itself is unusable.  Worth saying, but not
     * on every attempt: an App retrying 0x12 against a dead 8711 would
     * otherwise get a warning per retry.  First failure, then every 32nd. */
    s_submit_fails++;
    if (s_submit_fails == 1U || (s_submit_fails % 32U) == 0U)
    {
        EBADGE_WARN2("port_softap: ap_query failed %d (x%u)", rc,
                     (unsigned)s_submit_fails);
    }
    return rc;
}

/**
 * @brief  Spend one bounded boot attempt on filling the credential cache.
 *
 * Deliberately NOT periodic and NOT unbounded: that is the whole difference from
 * the poll this replaced.  Driven from the tick while attempts remain.
 *
 * An -EBUSY does not consume an attempt -- the AT layer being busy says nothing
 * about whether the 8711 would have answered, and spending one of three attempts
 * on an unrelated debug command's timing would be a waste.
 */
static void prime_cache_boot_try(void)
{
    if (s_prime_tries == 0U || wifi_8711_at_ap_cached(NULL))
    {
        return;
    }
    if (submit_query() != -EBUSY)
    {
        s_prime_tries--;
        if (s_prime_tries == 0U)
        {
            /* Say so once.  Otherwise "the AP info never turned up and nothing
             * is querying for it" reads as a hang rather than a decision. */
            EBADGE_LOG("port_softap: boot prime attempts spent; the cache is "
                       "now filled on demand only");
        }
    }
}

/**
 * @brief  A caller just missed the cache -- go and get it.
 *
 * Separate from the boot attempts and NOT limited by their budget: a phone
 * asking for credentials is a much better reason to use the AT link than a timer
 * is, and refusing because three silent attempts were already spent would make
 * the miss permanent.
 *
 * Rate-limited all the same, by SOFTAP_PRIME_RETRY_MS, because the trigger is
 * outside our control: an App may retry 0x12 as fast as it likes and each retry
 * lands here.
 */
static void prime_cache_on_demand(void)
{
    uint32_t now = ebadge_task_now_ms();

    if (wifi_8711_at_ap_cached(NULL))
    {
        return;
    }
    if (s_demand_valid && (int32_t)(now - s_next_demand_ms) < 0)
    {
        return;
    }
    s_demand_valid  = true;
    s_next_demand_ms = now + SOFTAP_PRIME_RETRY_MS;
    EBADGE_LOG("port_softap: cache miss -> querying the AP on demand");
    (void)submit_query();
}

#endif /* CONFIG_WIFI_8711 */

/*----------------------------------------------------------------------------*
 *  Tick -- on l2_task
 *----------------------------------------------------------------------------*/
static void on_tick(uint32_t now_ms)
{
#if defined(CONFIG_WIFI_8711)
    /* Two reasons to be on the wire, and only one of them is a poll:
     *   - a session is waiting for the phone to associate.  Polled, because the
     *     8711 has no way to tell us and the session cannot start without it.
     *   - the cache is empty and boot attempts remain.  Bounded, not polled.
     * Anything else and we stay quiet: the credentials are static, so there is
     * nothing a periodic query would learn. */
    bool want_join  = (s_running && s_joined_cb != NULL && !s_joined_reported);
    bool want_prime = (s_prime_tries != 0U) && !wifi_8711_at_ap_cached(NULL);

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
                               : SOFTAP_PRIME_RETRY_MS);

    if (want_join)
    {
        /* The join query refreshes the cache as a side effect, so there is no
         * point spending a prime attempt in the same breath. */
        (void)submit_query();
    }
    else
    {
        prime_cache_boot_try();
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
    /* Attempt the first prime on the very next tick rather than after an
     * interval: the first transfer offer can arrive as soon as the phone
     * connects over BLE, and without a cached SSID it is answered -EAGAIN.
     *
     * It will fail.  main() calls ebadge_task_init() before wifi_8711_init(),
     * so the link is not up yet and submit returns -ENODEV -- which is exactly
     * why the budget is SOFTAP_PRIME_TRIES and not one. */
    s_prime_tries  = SOFTAP_PRIME_TRIES;
    s_next_poll_ms = ebadge_task_now_ms();
    EBADGE_LOG1("port_softap: 8711 backend, priming AP cache (%u attempts, "
                "then on demand)", (unsigned)SOFTAP_PRIME_TRIES);
#else
    EBADGE_WARN("port_softap: CONFIG_WIFI_8711=n -- no radio, AP unavailable");
#endif
}

int ebadge_port_softap_start(ebadge_softap_info_t *out_info,
                             ebadge_ap_port_role_t role,
                             uint16_t *out_port,
                             ebadge_softap_sta_joined_cb_t joined_cb)
{
    if (out_info == NULL || out_port == NULL)
    {
        return -EINVAL;
    }

    if (!ebadge_port_softap_info(out_info, role, out_port))
    {
        /* Nothing known yet.  Do NOT pretend success: the session would emit a
         * 0x13 full of zeroes and the phone would try to join a network called
         * "".  -EAGAIN maps to NOT_READY on the wire, which is the truth.
         *
         * info() has already provoked a query on its way out, so the App's retry
         * of this same offer is what finds the cache warm.  Nothing waits for it
         * here -- l2_task must not block -- so this offer still fails. */
        EBADGE_WARN("port_softap: start with no AP info cached -> EAGAIN");
        return -EAGAIN;
    }

    if (*out_port == 0U)
    {
        /* Credentials known but the port for this role is not.  Refuse rather
         * than proceed: the session would emit a 0x13 with port 0, the phone
         * would associate, fail to connect anywhere, and the only symptom would
         * be the 60 s WAIT_STA deadline expiring.  -EAGAIN is the same "retry"
         * answer as an empty cache because the remedy is the same -- a WLSTATE
         * reply, which only carries the ports, has to land first.
         *
         * info() has already provoked a query if the cache was empty; here the
         * cache is valid but partial (a WLSTARTAP-only fill), so ask outright. */
        EBADGE_WARN1("port_softap: %s port unknown (WLSTATE not answered yet)"
                     " -> EAGAIN",
                     (role == EBADGE_AP_PORT_STREAM) ? "stream" : "file");
#if defined(CONFIG_WIFI_8711)
        (void)submit_query();
#endif
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

bool ebadge_port_softap_info(ebadge_softap_info_t *out_info,
                             ebadge_ap_port_role_t role,
                             uint16_t *out_port)
{
#if defined(CONFIG_WIFI_8711)
    wifi_8711_ap_info_t ap;

    if (!wifi_8711_at_ap_cached(&ap))
    {
        /* The link has never answered.  Provoke a query on the way out, so that
         * "retry later" is advice the device is acting on rather than a hope --
         * this is the only thing that fills the cache once the boot attempts are
         * spent.  It does not help THIS call: nothing here waits for the reply,
         * because both callers are on l2_task and must not block.  The App's
         * retry is what finds the cache warm. */
        prime_cache_on_demand();
        return false;
    }

    if (ap.ssid[0] == '\0')
    {
        /* Answered, but with no AP configured.  Deliberately NOT provoking a
         * retry: the 8711 cannot change its own credentials, so asking again
         * would return the same empty SSID -- and doing it per caller would
         * reinstate exactly the idle poll this module got rid of. */
        return false;
    }

    if (out_info != NULL)
    {
        memset(out_info, 0, sizeof(*out_info));
        memcpy(out_info->ssid, ap.ssid, sizeof(out_info->ssid) - 1U);
        memcpy(out_info->password, ap.password, sizeof(out_info->password) - 1U);
        out_info->ip = ap.ip;
        /* From the CHANNEL= line of AT+WLSTATE (spi-at-command-protocol sec.10.1,
         * v2.1).  Still 0 against an older 8711 build that omits the line, which
         * remains a safe answer: the phone scans for the SSID, and 0x13's channel
         * TLV is a hint rather than a tuning instruction. */
        out_info->channel = ap.channel;
    }
    if (out_port != NULL)
    {
        /* The caller named a role, so hand back the matching port and nothing
         * else.  A 0 here means the 8711 has not reported that port yet -- only
         * a WLSTATE reply carries them, and a WLSTARTAP-only cache has neither.
         * Reported as 0 rather than substituted with a default: a wrong-but-
         * plausible port sends the phone to a door that silently drops it,
         * which is far harder to diagnose than an obviously absent one. */
        *out_port = (role == EBADGE_AP_PORT_STREAM) ? ap.stream_port
                    : ap.file_port;
        if (*out_port == 0U)
        {
            EBADGE_WARN1("port_softap: 8711 has not reported the %s port yet",
                         (role == EBADGE_AP_PORT_STREAM) ? "stream" : "file");
        }
    }
    return true;
#else
    (void)out_info; (void)role; (void)out_port;
    return false;
#endif
}
