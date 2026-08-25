/**
 * @file    ebadge_port_softap.c
 * @brief   SoftAP port backed by the RTL8711FA over SPI.
 *
 * Two jobs, and they are not the same job:
 *
 *   1. Answer "what is the AP and is it usable" synchronously, because the BLE
 *      handlers that ask run on l2_task and an AT round trip costs ~10 s.
 *   2. Turn "a station associated" into a callback, which the 8711 does not
 *      offer as an event.
 *
 * Both are now answered from the global Wi-Fi state in wifi_8711_at_ap.c, which
 * the 8711 feeds on its own about once a second (protocol v3.1 sec.7.1: the POLL
 * payload carries the whole state block).  So this file asks almost nothing --
 * it reads.
 *
 * ---------------------------------------------------------------------------
 * WHAT REPLACED THE POLLS -- ALL THREE OF THEM
 * ---------------------------------------------------------------------------
 * This module used to put AT+WLSTATE on the wire from three different timers: a
 * 15 s join poll while a session waited for the phone, a bounded burst of boot
 * attempts to fill a credential cache, and an on-demand query whenever a caller
 * missed that cache.  All three existed for one reason -- the state only arrived
 * when we asked -- and all three are gone now that it arrives on its own:
 *
 *   the joined edge   read off CLIENTS= in the global state, on the tick.  The
 *                     edge is now found within ~100 ms of the beat that shows it
 *                     instead of up to 15 s late.
 *   the credentials   already there.  A caller reads the state; there is no
 *                     cache to prime and nothing to prime it from.
 *   staleness         the ONE query left (state_probe()).  Not a poll of the
 *                     state -- a check that the FEED is alive, which only fires
 *                     when the feed has already stopped.
 *
 * The net effect on the AT link is the opposite of what it looks like: a healthy
 * idle device now sends nothing at all, because the 6 s rule never trips.
 *
 * ---------------------------------------------------------------------------
 * FRESHNESS IS THE WHOLE PRECONDITION
 * ---------------------------------------------------------------------------
 * Everything this file hands out is guarded by state_fresh(), and that is not
 * belt-and-braces.  The state survives things it describes: the credentials are
 * merged rather than overwritten (they cannot change, so an AP=DOWN block must
 * not erase them), and a BLE disconnect switches the radio off.  So a block from
 * ten seconds ago can name an SSID, a password and two ports that are all still
 * literally correct while there is no radio on the air -- which is exactly the
 * shape that sends a phone off to join a network that is not there and leaves it
 * to time out with no diagnostic.
 *
 * A fresh block, by contrast, is the 8711 saying so this second.  That is why
 * SOFTAP_STATE_STALE_MS gates the readers as well as the probe.
 */
#include <errno.h>
#include <string.h>

#include "ebadge_port_softap.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#if defined(CONFIG_WIFI_8711)
#include "wifi_8711_at.h"       /* WIFI_8711_AT_TIMEOUT_MS, for the probe gap  */
#include "wifi_8711_at_ap.h"
#endif

/** How old the global Wi-Fi state may be and still be acted on.
 *
 *  The 8711 describes itself about once a second (sec.7.4 on the POLL payload),
 *  so anything past a few beats means the feed has stopped rather than that the
 *  news is merely a little old -- and a stopped feed is a transport fault, not a
 *  slow AP.  Six seconds is about five beats: comfortably clear of a couple of
 *  missed slots during a JPEG stream, and short enough that the state a caller
 *  acts on still describes a radio that was confirmed alive within the time a
 *  phone takes to scan.
 *
 *  It replaces nothing -- there was no staleness rule at all before, because the
 *  state only changed when we asked for it and "as of the last time we asked" was
 *  the best anything could offer. */
#define SOFTAP_STATE_STALE_MS   6000u

/** Attempts the BLE-connect sequence gets before it gives up.
 *
 *  Spent on asking for the AP and on waiting out a bring-up, so this is not
 *  "one": a WLSTARTAP can be refused with -EBUSY by the single-flight AT layer,
 *  and an accepted one leaves the AP STARTING for seconds to tens of seconds
 *  (the 8711 retries internally every 5 s).  Four spans ~60 s at the retry rate
 *  below, past the point where the radio either comes up or does not -- and
 *  giving up costs less than it used to: the state keeps arriving on the beat, so
 *  an AP that comes up after the sequence has ended is still noticed. */
#define SOFTAP_ARM_TRIES            4u

/** Gap between arm attempts.  Sized off the AT round trip, which is ~10 s on
 *  hardware and single-flight: anything faster collects -EBUSY instead of asking
 *  a question. */
#define SOFTAP_ARM_RETRY_MS     15000u

#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
/** How long after a transfer ends before the radio goes down.
 *
 *  Not a guess at "long enough to be polite" -- it is sized off what is still in
 *  flight when the caller asks.  [+XFERACK]:OK means the 8711 ACCEPTED the ack
 *  command (spec sec.10.3); it then builds the EBXR and closes the TCP connection
 *  itself, and the 0x15/0x16 notify has only just been queued on the BLE link.
 *  Three seconds clears all of that with room to spare on a link whose own beat is
 *  ~1 s, and it is short enough that a phone which is genuinely done sees the
 *  hotspot disappear rather than linger.
 *
 *  It also acts as a coalescing window: a phone sending images back to back
 *  re-claims the AP inside it, and ebadge_port_softap_start() cancels the schedule
 *  -- so the common burst case pays no teardown at all. */
#define SOFTAP_IDLE_DOWN_MS     3000u
#endif /* EBADGE_SOFTAP_PER_TRANSFER_LIFETIME */

static bool                          s_running;
static ebadge_softap_sta_joined_cb_t s_joined_cb;
static bool                          s_joined_reported;

#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
/*  Deferred AP teardown, for a transfer that has just reported its verdict -- see
 *  ebadge_port_softap_shutdown_when_idle() and SOFTAP_IDLE_DOWN_MS.  Declared up
 *  here with the rest of the module state because the arm path cancels it, and
 *  that path is written above the shutdown block that owns it.
 *
 *  A timestamp plus a bool rather than a countdown: the arithmetic then matches
 *  every other deadline in this file, and "due at t=0" stays representable across
 *  an uptime wrap.  Touched only on l2_task. */
static bool                          s_idle_down_pending;
static uint32_t                      s_idle_down_at_ms;
#endif /* EBADGE_SOFTAP_PER_TRANSFER_LIFETIME */

#if defined(CONFIG_WIFI_8711)

/*----------------------------------------------------------------------------*
 *  Freshness
 *----------------------------------------------------------------------------*/

/**
 * Read the global Wi-Fi state, but only admit it if the 8711 said it recently.
 *
 * The single gate in front of every reader here -- see "FRESHNESS IS THE WHOLE
 * PRECONDITION" in the file header for why a stale block is worse than no block
 * rather than merely less good.
 *
 * @return false if no block has ever arrived OR the feed has stopped.  @p ap may
 *         have been written either way, so a caller must not read it on false.
 */
static bool state_fresh(wifi_8711_ap_info_t *ap)
{
    if (!wifi_8711_at_ap_cached(ap))
    {
        return false;
    }
    return wifi_8711_at_ap_cache_age_ms() <= SOFTAP_STATE_STALE_MS;
}

/*----------------------------------------------------------------------------*
 *  The one remaining query -- a liveness check on the feed
 *
 *  Rate-limited to one in flight per AT deadline rather than one per 6 s: the
 *  layer is single flight, so a second probe submitted while the first is still
 *  outstanding is not a second question, it is an -EBUSY.  Sizing the gap off
 *  WIFI_8711_AT_TIMEOUT_MS makes every probe a real question.
 *----------------------------------------------------------------------------*/
#define SOFTAP_PROBE_GAP_MS     WIFI_8711_AT_TIMEOUT_MS

static uint32_t s_next_probe_ms;

/* Consecutive submit failures, for log rate-limiting only.  A board with no
 * working 8711 stays stale forever, so this path repeats for the life of the
 * device and a warning per attempt would bury everything else. */
static uint32_t s_probe_fails;

/*----------------------------------------------------------------------------*
 *  BLE-connect arm sequence
 *
 *  Driven from the tick rather than chained off the AT callbacks, even though
 *  the AT layer does allow a callback to submit the next command (see the
 *  CONCURRENCY note in wifi_8711_at.c).  Chaining would be tighter but it only
 *  covers the success path: a WLSTARTAP that is refused, times out, or never
 *  gets staged has no callback to hang the follow-up on, and that is precisely
 *  the case worth retrying.  The tick already exists and already owns the
 *  retry-with-backoff shape, so the sequence is expressed as state it reads.
 *
 *  It is ONE command now.  It used to be WLSTARTAP followed by WLSTATE, because
 *  only a WLSTATE reply carried PORT= / FILE_PORT= -- and that second command was
 *  the whole reason the sequence needed a stage machine.  Since sec.7.1 the ports
 *  arrive on the next beat by themselves, so the follow-up would be asking for
 *  something already on its way.
 *----------------------------------------------------------------------------*/
static bool     s_arm_active;
static uint8_t  s_arm_tries;
static uint32_t s_next_arm_ms;

/** How recently the data plane must have carried a slot for that alone to settle
 *  the question of whether the AP is up.
 *
 *  Sized off what the traffic proves rather than off a beat.  A JPGS or EBFS slot
 *  means a phone was associated and pushing bytes through a TCP connection on this
 *  radio, and an AP does not stop being up because a transfer paused: the gaps here
 *  are the phone's -- a stream between frames, an upload between chunks, a file
 *  finished while the phone stays joined.  Six seconds matches
 *  SOFTAP_STATE_STALE_MS so the two kinds of evidence expire together, which keeps
 *  "the AP is usable" one answer instead of two that can disagree. */
#define SOFTAP_DATAPLANE_LIVE_MS    6000u

/**
 * True when the state says the phone can be handed an endpoint right now.
 *
 * Stricter than "a block has arrived" on three counts, each of which was a bug at
 * some point: it must be FRESH (a stale block can describe a radio that a BLE
 * disconnect has since switched off), the AP must be UP (STARTING means the
 * credentials in the same block are not usable yet), and BOTH ports must be
 * known -- which one a session needs depends on whether the phone sends a file or
 * opens a preview, and that is not known here.
 */
static bool arm_target_met(void)
{
    wifi_8711_ap_info_t ap;

    if (!state_fresh(&ap))
    {
        return false;
    }
    if (ap.state != WIFI_8711_AP_UP)
    {
        return false;
    }
    return (ap.ssid[0] != '\0') && (ap.stream_port != 0U) && (ap.file_port != 0U);
}

/**
 * True when Wi-Fi traffic proves the AP is up, whatever the state block says.
 *
 * This outranks arm_target_met() rather than supplementing it, and the reason is
 * that the two can genuinely disagree.  arm_target_met() is the 8711 DESCRIBING
 * its radio; this is a phone having joined that radio and pushed bytes through a
 * TCP connection on it.  When a description says DOWN while data is arriving
 * through the thing it describes, the data is right.
 *
 * WHAT IT PREVENTS.  A WLSTARTAP sent while a transfer is running is not free and
 * not harmless.  The AT link is single-flight with a ~10 s round trip, so that
 * command occupies the link an EBFS stream is sharing, and it asks the 8711 to
 * bring up a radio a phone is currently associated with -- which at best is
 * answered ERROR and at worst disturbs the association mid-upload.  Any of the
 * ways the state block can go momentarily unusable -- a stale beat during a busy
 * stream, a block that reports STARTING, a missing port -- would otherwise be
 * enough to trigger exactly that.
 *
 * WHY THE STALENESS BUDGET IS SEPARATE.  The 6 s here is not the state's 6 s
 * repurposed: it is how long a phone may sit between slots and still be considered
 * present.  They happen to be the same number and are kept as two constants so
 * that changing what "the feed stopped" means does not silently change what
 * "the phone left" means.
 */
static bool dataplane_says_ap_up(void)
{
    return wifi_8711_at_dataplane_age_ms() <= SOFTAP_DATAPLANE_LIVE_MS;
}

/** Runs on l2_task, from ebadge_port_softap_arm().
 *
 *  Also reached synchronously from ebadge_port_softap_start(), which already runs
 *  on l2_task -- hence the @p why text: the two callers are asking for the same
 *  sequence for different reasons, and a log line that always said "BLE connected"
 *  would misattribute the mid-connection re-arm. */
static void arm_begin(const char *why)
{
#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
    /* Whatever wants the AP up outranks a pending order to switch it off.  Both
     * callers are that: a phone appearing, or a session claiming the radio.
     *
     * Nothing schedules a teardown in the default arrangement, so there is nothing
     * to outrank. */
    s_idle_down_pending = false;
#endif

    if (arm_target_met())
    {
        /* Nothing to ask: the AP is up and this second's block says so, which is
         * everything an offer needs.  Spending a WLSTARTAP to be told the same
         * thing would only occupy the single-flight layer. */
        EBADGE_LOG1("port_softap: %s, AP already up and usable", why);
        return;
    }

    if (dataplane_says_ap_up())
    {
        /* Checked SECOND but it overrides the FIRST: getting here means the state
         * block is unusable -- stale, DOWN, STARTING, or missing a port -- while
         * Wi-Fi traffic is arriving through the very radio it describes.  The
         * traffic wins, so there is nothing to ask for.
         *
         * This is the case the ordering exists for.  A phone mid-upload holds the
         * AP up by definition, and a WLSTARTAP aimed at it would occupy the
         * single-flight AT link the EBFS stream shares for a ~10 s round trip. */
        EBADGE_LOG1("port_softap: %s, but Wi-Fi data is flowing -- AP is up, not "
                    "asking", why);
        return;
    }

    if (s_arm_active)
    {
        /* Already asking.  Restarting the sequence would reset the attempt budget
         * that bounds the wait, so an offer arriving once a beat during a slow
         * bring-up could hold it open indefinitely. */
        EBADGE_LOG1("port_softap: %s, AP bring-up already in progress", why);
        return;
    }

    s_arm_active = true;
    s_arm_tries  = SOFTAP_ARM_TRIES;
    /* First attempt on the next tick, not after an interval: the phone may send
     * its offer within a second of connecting, and every tick spent waiting is
     * one the App may have to answer a NOT_READY for. */
    s_next_arm_ms = ebadge_task_now_ms();
    EBADGE_LOG1("port_softap: %s -> starting the AP (WLSTARTAP)", why);
}

static void arm_on_l2(void *arg)
{
    (void)arg;
    arm_begin("BLE connected");
}

/**
 * Spend one attempt on the arm sequence.  Called from the tick with the
 * schedule already advanced.
 */
static void arm_step(void)
{
    wifi_8711_ap_info_t ap;
    bool                fresh = state_fresh(&ap);

    if (arm_target_met())
    {
        EBADGE_LOG("port_softap: AP is up, arm sequence done");
        s_arm_active = false;
        return;
    }

    if (dataplane_says_ap_up())
    {
        /* The sequence is finished, not merely postponed.  A phone is pushing Wi-Fi
         * traffic through this AP, so whatever the state block is failing to say,
         * the radio it describes is up and in use.
         *
         * This gate matters more than the one in arm_begin(), because of when it
         * runs.  arm_begin() fires on the BLE connect, before any transfer exists,
         * so it almost always sees no data plane and starts the sequence -- which is
         * correct.  The retries then land 15 s apart, by which time a transfer may
         * well be running, and THIS is where such a retry gets stopped.  Without it
         * a single stale beat during a busy EBFS stream would put a ~10 s WLSTARTAP
         * on the single-flight link the stream is sharing. */
        EBADGE_LOG("port_softap: Wi-Fi data is flowing -- AP is up, arm sequence "
                   "abandoned");
        s_arm_active = false;
        return;
    }

    if (s_arm_tries == 0U)
    {
        /* Not a dead end.  The state keeps arriving on the beat, so an AP that
         * comes up later is picked up by ebadge_port_softap_info() without
         * anything re-arming -- what is spent is our patience, not the feed. */
        EBADGE_WARN("port_softap: AP arm attempts spent; offers are answered "
                    "NOT_READY until the state block reports AP=UP");
        s_arm_active = false;
        return;
    }

    if (fresh && ap.state == WIFI_8711_AP_STARTING)
    {
        /* Deliberately NOT re-sending.  The request was accepted and the 8711 is
         * retrying the bring-up internally every 5 s, so another WLSTARTAP asks
         * for something already in progress.  The attempt is spent all the same,
         * which is what bounds the wait -- otherwise an AP stuck in STARTING would
         * hold the sequence open for the life of the connection. */
        s_arm_tries--;
        EBADGE_LOG1("port_softap: AP bring-up in progress, waiting (%u attempts "
                    "left)", (unsigned)s_arm_tries);
        return;
    }

    /* No completion callback: there is nothing left for one to do.  The verdict is
     * logged by wifi_8711_at_ap.c, the resulting state arrives on the beat, and
     * the joined edge is read off that same state by the tick -- so a callback
     * here would only be a second path to facts we already have. */
    int rc = wifi_8711_at_ap_start(NULL, NULL);

    if (rc == -EBUSY)
    {
        /* Routine: something else holds the single-flight layer.  Costs no
         * attempt -- the question was never asked, so it still needs asking. */
        return;
    }

    s_arm_tries--;

    if (rc != 0)
    {
        EBADGE_WARN2("port_softap: arm submit failed rc=%d (%u attempts left)",
                     rc, (unsigned)s_arm_tries);
    }
}

/*----------------------------------------------------------------------------*
 *  AP shutdown
 *
 *  One stop at a time.  The flag is written on l2_task (shutdown) and cleared on
 *  the AT callback thread (stop_done), which is a plain bool store from each side
 *  with no read-modify-write -- and the only consequence of losing the race is one
 *  redundant AT command, which the 8711 answers with ERROR and nothing acts on.
 *  A mutex for that would be more machinery than the risk.
 *----------------------------------------------------------------------------*/
static bool s_stop_inflight;

/** AT-layer completion.  Nothing to decide -- see wifi_8711_at_ap_stop() on why a
 *  refusal is not retried -- so this only releases the in-flight flag and says
 *  what happened. */
static void stop_done(bool ok, void *user)
{
    (void)user;
    s_stop_inflight = false;
    if (!ok)
    {
        /* wifi_8711_at_ap.c has already logged which failure shape came back. */
        EBADGE_LOG("port_softap: AP stop not confirmed (already down, or a start "
                   "raced us) -- the next beat says which, and the next transfer "
                   "re-raises it anyway");
    }
}

#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
/**
 * Fire a scheduled idle-down, if it is due and still wanted.
 *
 * The "still wanted" half is the point.  Between arming the schedule and this
 * firing, a preview stream or a second file transfer may have claimed the AP --
 * both go through ebadge_port_softap_start(), which cancels the schedule, but the
 * check is repeated here because the two sessions share this one radio and a
 * teardown owned by whichever finished first must never cut off whichever started
 * next.
 */
static void idle_down_check(uint32_t now_ms)
{
    if (!s_idle_down_pending)
    {
        return;
    }
    if ((int32_t)(now_ms - s_idle_down_at_ms) < 0)
    {
        return;
    }
    s_idle_down_pending = false;

    if (s_running)
    {
        /* Belt and braces against the shared-radio case above.  Not a warning: a
         * stream that starts inside the settling window is normal traffic, not a
         * fault. */
        EBADGE_LOG("port_softap: idle-down due but a session has the AP -- keeping "
                   "it up");
        return;
    }
    if (s_arm_active)
    {
        /* A bring-up is on the wire.  Stopping the AP we are in the middle of
         * asking for would leave the two commands racing on a single-flight link
         * for no benefit -- and something asked for it, so it is wanted. */
        EBADGE_LOG("port_softap: idle-down due but an arm is in flight -- skipped");
        return;
    }

    EBADGE_LOG1("port_softap: transfer finished %u ms ago and nothing claimed the "
                "AP -> stopping it", (unsigned)SOFTAP_IDLE_DOWN_MS);
    ebadge_port_softap_shutdown();
}
#endif /* EBADGE_SOFTAP_PER_TRANSFER_LIFETIME */

/*----------------------------------------------------------------------------*
 *  Tick -- on l2_task
 *----------------------------------------------------------------------------*/

/**
 * Deliver the joined edge to a waiting session.
 *
 * Reads CLIENTS= out of the global state instead of provoking a query for it,
 * which is what removed the join poll.  No thread hop either: the tick runs on
 * l2_task, which is where the session callback has to be called, so the edge is
 * found and delivered in the same breath -- the post_call and the re-checks on
 * the far side of it went with the poll.
 *
 * Freshness matters more here than anywhere: acting on a client count from a link
 * that has gone quiet would start a transfer against a phone that may have left.
 */
static void joined_check(void)
{
    wifi_8711_ap_info_t ap;

    if (!s_running || s_joined_cb == NULL || s_joined_reported)
    {
        return;
    }
    if (!state_fresh(&ap) || ap.clients == 0U)
    {
        return;
    }

    /* Latched before the call, not after: the callback runs a session state
     * machine that can come back through this module, and a second edge would
     * resurrect a transfer that has already moved on. */
    s_joined_reported = true;

    ebadge_softap_sta_joined_cb_t cb = s_joined_cb;
    EBADGE_LOG1("port_softap: STA associated (clients=%u) -> notifying session",
                (unsigned)ap.clients);
    cb();
}

/**
 * The 6 s rule: if the state has not been refreshed, ask for it once.
 *
 * This is a check on the FEED, not a poll of the state.  On a healthy link the
 * age never reaches SOFTAP_STATE_STALE_MS and this function never submits
 * anything, which is why an idle device is now silent on the AT link.  When it
 * does fire, the question being asked is "does the link answer at all" -- and the
 * answer, either way, is what everything else here is gated on.
 *
 * It also covers the cold start, without a boot-time special case: the transport
 * is not running until somebody submits a command (see wifi_8711_at.h), so
 * nothing arrives and the age is UINT32_MAX -- which trips this rule on the first
 * tick and starts the very feed it is checking.
 */
static void state_probe(uint32_t now_ms)
{
    if (s_arm_active)
    {
        /* The arm sequence is already on the wire and its reply refreshes the
         * state.  Two submitters would just hand each other -EBUSY, and the arm is
         * the one with a phone waiting on it. */
        return;
    }
    if (wifi_8711_at_ap_cache_age_ms() <= SOFTAP_STATE_STALE_MS)
    {
        return;
    }
    if ((int32_t)(now_ms - s_next_probe_ms) < 0)
    {
        return;
    }
    /* Advance the schedule before submitting, not after: on -EBUSY we still want
     * to back off rather than retry on every tick. */
    s_next_probe_ms = now_ms + SOFTAP_PROBE_GAP_MS;

    int rc = wifi_8711_at_ap_query(NULL, NULL);
    if (rc == 0 || rc == -EBUSY)
    {
        /* -EBUSY is routine -- the AT layer is single flight and something else
         * has it, which is itself evidence the link is being used. */
        s_probe_fails = 0U;
        return;
    }
    /* Anything else means the link itself is unusable.  First failure, then every
     * 32nd: this repeats forever on a board with no 8711. */
    s_probe_fails++;
    if (s_probe_fails == 1U || (s_probe_fails % 32U) == 0U)
    {
        EBADGE_WARN2("port_softap: Wi-Fi state feed stopped and the probe will "
                     "not submit, rc=%d (x%u)", rc, (unsigned)s_probe_fails);
    }
}

#endif /* CONFIG_WIFI_8711 */

/* on_tick() itself is compiled either way -- ebadge_task_set_tick() takes it
 * unconditionally -- so it guards its body rather than sitting inside the block
 * above with the helpers it calls. */
static void on_tick(uint32_t now_ms)
{
#if defined(CONFIG_WIFI_8711)
    /* The arm sequence keeps its own deadline rather than sharing the probe's,
     * because the two answer different questions and a probe that has just backed
     * off must not also postpone the AP bring-up a phone is waiting on. */
    if (s_arm_active && (int32_t)(now_ms - s_next_arm_ms) >= 0)
    {
        /* Advance the schedule before stepping, so an -EBUSY backs off instead of
         * retrying on every tick. */
        s_next_arm_ms = now_ms + SOFTAP_ARM_RETRY_MS;
        arm_step();
    }

    /* Reading first, asking second, and only asking when the reading is too old
     * to use -- which is the whole shape of this module now. */
    joined_check();
    state_probe(now_ms);
#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
    /* Last, and after joined_check(): a station associating is a session about to
     * claim the AP, and evaluating the teardown before that edge is delivered would
     * judge "is anything using it" a beat too early. */
    idle_down_check(now_ms);
#endif
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
    /* Seeded with a real timestamp rather than left at 0, so the comparison in
     * state_probe() is always between two uptimes -- at 0 the wrap arithmetic
     * would read it as "the future" once uptime passed ~24 days.
     *
     * The first probe therefore lands on the very next tick, which is what starts
     * the transport and with it the state feed. */
    s_next_probe_ms = ebadge_task_now_ms();
    EBADGE_LOG1("port_softap: 8711 backend, Wi-Fi state is pushed to us (~1 Hz);"
                " querying only if it stops for %u ms",
                (unsigned)SOFTAP_STATE_STALE_MS);
#else
    EBADGE_WARN("port_softap: CONFIG_WIFI_8711=n -- no radio, AP unavailable");
#endif
}

void ebadge_port_softap_arm(void)
{
#if defined(CONFIG_WIFI_8711)
    /* Marshalled onto l2_task rather than acted on here: the caller is the GAP
     * dispatcher on the BT stack thread, and the sequence state above is read and
     * written by the tick, which runs on l2_task.  post_call is the same hop the
     * disconnect path already uses for the session aborts.
     *
     * A caller already on l2_task wants ebadge_port_softap_arm_on_l2() instead --
     * this hop would put the arm after their current handler returns. */
    (void)ebadge_task_post_call(arm_on_l2, NULL);
#else
    EBADGE_LOG("port_softap: no radio -- BLE connect does not start an AP");
#endif
}

#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
void ebadge_port_softap_arm_on_l2(const char *why)
{
#if defined(CONFIG_WIFI_8711)
    arm_begin((why != NULL) ? why : "AP requested");
#else
    (void)why;
    EBADGE_LOG("port_softap: no radio -- nothing to start");
#endif
}
#endif /* EBADGE_SOFTAP_PER_TRANSFER_LIFETIME */

int ebadge_port_softap_start(ebadge_softap_info_t *out_info,
                             ebadge_ap_port_role_t role,
                             uint16_t *out_port,
                             ebadge_softap_sta_joined_cb_t joined_cb)
{
    if (out_info == NULL || out_port == NULL)
    {
        return -EINVAL;
    }

#if defined(CONFIG_WIFI_8711) && EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
    /* A session is claiming the radio, so cancel any scheduled idle-down before
     * anything else -- including on the refusal path below, where the App is about
     * to retry and the AP going down underneath that retry is the one outcome that
     * makes it hopeless.
     *
     * Nothing to cancel in the default arrangement: the radio is not scheduled down
     * by a transfer there, it goes down on the BLE disconnect. */
    s_idle_down_pending = false;
#endif

    if (!ebadge_port_softap_info(out_info, role, out_port))
    {
        /* Do NOT pretend success: the session would emit a 0x13 full of zeroes and
         * the phone would try to join a network called "".  -EAGAIN maps to
         * NOT_READY on the wire, which is the truth.
         *
         * Retrying is cheap now and usually works: the state arrives every second,
         * so an offer refused because the AP was still STARTING is answerable a
         * beat later.  Nothing waits for it here -- l2_task must not block -- so
         * this offer still fails. */
        EBADGE_WARN("port_softap: no usable AP state -> EAGAIN (retry in a beat)");
#if defined(CONFIG_WIFI_8711) && EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
        /* And make the retry answerable.  Since v3.1 sec.12.2 the 8711 does not
         * raise its own AP, and under this arrangement the radio does not stay up
         * for the whole BLE connection -- a finished transfer schedules it down.  So
         * "down" here is a routine mid-connection state, not a broken one, and
         * without this the second transfer of a connection would collect AP_START
         * forever.
         *
         * Not needed in the default arrangement, and deliberately absent from it: the
         * AP is armed on the BLE connect and lowered on the disconnect, so a "down"
         * seen here is a bring-up still running or a fault -- and re-arming a fault
         * on every offer would only spend AT round trips on a single-flight link.
         *
         * arm_begin() is a no-op when a bring-up is already under way or the AP is
         * merely STARTING, so an App that retries once a second does not restart
         * anything. */
        arm_begin("session needs the AP");
#endif
        return -EAGAIN;
    }

    if (*out_port == 0U)
    {
        /* AP up and credentials known, but not the port for this role.  Refuse
         * rather than proceed: the session would emit a 0x13 with port 0, the
         * phone would associate, fail to connect anywhere, and the only symptom
         * would be the 60 s WAIT_STA deadline expiring.
         *
         * Nothing is asked here.  The block is fresh -- info() would not have
         * returned true otherwise -- so this port is missing from the 8711's own
         * current description of itself, and asking again a millisecond later
         * would get the same answer.  The next beat is what can change it. */
        EBADGE_WARN1("port_softap: %s port missing from a fresh state block"
                     " -> EAGAIN", (role == EBADGE_AP_PORT_STREAM) ? "stream"
                     : "file");
        return -EAGAIN;
    }

    s_joined_cb       = joined_cb;
    s_joined_reported = false;
    s_running         = true;
    /* No schedule to seed: joined_check() reads the global state on every tick, so
     * a phone that has already associated is noticed within ~100 ms.  This is
     * where the 15 s join poll used to be armed. */

    EBADGE_LOG2("port_softap: session using ssid=\"%s\" port=%u",
                out_info->ssid, (unsigned)*out_port);
    return 0;
}

int ebadge_port_softap_stop(void)
{
    if (s_running)
    {
        /* Say plainly that the radio is still up at THIS point, so a log reader
         * does not go looking here for the teardown -- it happens on the BLE
         * disconnect, elsewhere and later. */
        EBADGE_LOG("port_softap: session released (radio still up -- it goes down "
                   "on the BLE disconnect)");
    }
    s_running         = false;
    s_joined_cb       = NULL;
    s_joined_reported = false;
    return 0;
}

#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
void ebadge_port_softap_shutdown_when_idle(void)
{
#if defined(CONFIG_WIFI_8711)
    /* Scheduled rather than done, and the delay is the load-bearing part -- see
     * SOFTAP_IDLE_DOWN_MS on what is still in flight at the moment the caller asks.
     * Re-arming an existing schedule restarts the window, which is what makes a
     * burst of transfers coalesce into one teardown at the end. */
    s_idle_down_at_ms   = ebadge_task_now_ms() + SOFTAP_IDLE_DOWN_MS;
    s_idle_down_pending = true;
    EBADGE_LOG1("port_softap: transfer done -> AP down in %u ms unless something "
                "claims it", (unsigned)SOFTAP_IDLE_DOWN_MS);
#else
    EBADGE_LOG("port_softap: no radio -- nothing to schedule down");
#endif
}
#endif /* EBADGE_SOFTAP_PER_TRANSFER_LIFETIME */

void ebadge_port_softap_shutdown(void)
{
#if defined(CONFIG_WIFI_8711)
#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
    /* Whether we submit below or bail out, there is no longer a deferred stop worth
     * keeping: either one is on the wire or one was refused, and the schedule would
     * only fire a duplicate. */
    s_idle_down_pending = false;
#endif

    if (s_stop_inflight)
    {
        /* One BLE disconnect is one stop, but nothing stops the GAP layer reporting
         * a disconnect twice, and on a single-flight AT link the second submit would
         * only be refused with -EBUSY.  Collapsing them costs nothing: the command
         * has no arguments, so the one in flight is the same one. */
        return;
    }

    /* Cancel any arm sequence still owed.  A phone that connects and disconnects
     * inside the sequence's retry window leaves it mid-flight, and the next tick
     * would then ask for an AP we are switching off in the same breath.  The next
     * connection re-arms from the top. */
    s_arm_active = false;

    int rc = wifi_8711_at_ap_stop(stop_done, NULL);
    if (rc != 0)
    {
        /* Not retried, deliberately.  The AP being left up is a wasted beacon, not
         * a broken transfer, and the next start() re-raises it regardless -- so
         * there is no state to repair and nothing a retry would protect. */
        EBADGE_WARN1("port_softap: WLSTOPAP submit failed rc=%d (AP left up)", rc);
        return;
    }
    s_stop_inflight = true;
    EBADGE_LOG("port_softap: stopping the AP (WLSTOPAP)");
#else
    EBADGE_LOG("port_softap: no radio -- nothing to stop");
#endif
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

    /* Answered from the global state, and NOTHING is put on the wire here.  That
     * is the point: this runs on l2_task from a BLE handler that must answer in
     * milliseconds, and the state it reads is not a cached approximation of what
     * the 8711 would say -- it IS what the 8711 said, at most one beat ago.
     *
     * A query used to be provoked on the way out, on the grounds that "retry
     * later" should be advice the device is acting on.  It no longer is: the feed
     * refreshes itself, and if it has stopped, state_probe() on the tick is
     * already asking.  One submitter, one reason. */
    if (!state_fresh(&ap))
    {
        /* Either the link has never spoken or it has stopped.  Both are "we cannot
         * describe an AP right now", which is a different answer from "there is no
         * AP" -- and the caller's move is the same either way: NOT_READY, and let
         * the App retry. */
        return false;
    }

    if (ap.state != WIFI_8711_AP_UP)
    {
        /* DOWN or STARTING.  Refused even though the credentials below may be
         * populated, and that is the load-bearing case rather than a corner: the
         * global state MERGES credentials (they cannot change, so an AP=DOWN block
         * must not erase them), so a down AP reports the last connection's SSID and
         * ports quite truthfully.  Handing those out would send the phone to join a
         * network whose radio is off. */
        return false;
    }

    if (ap.ssid[0] == '\0')
    {
        /* AP=UP with no SSID should not happen -- sec.7.4 fills the credentials
         * whenever the state is UP.  Refused rather than reported as success,
         * because the phone cannot look for a nameless network. */
        EBADGE_WARN("port_softap: AP=UP but no SSID in the state block");
        return false;
    }

    if (out_info != NULL)
    {
        memset(out_info, 0, sizeof(*out_info));
        memcpy(out_info->ssid, ap.ssid, sizeof(out_info->ssid) - 1U);
        memcpy(out_info->password, ap.password, sizeof(out_info->password) - 1U);
        out_info->ip = ap.ip;
        /* From the CHANNEL= line of the state block.  Still 0 against an older
         * 8711 build that omits the line, which remains a safe answer: the phone
         * scans for the SSID, and 0x13's channel TLV is a hint rather than a tuning
         * instruction. */
        out_info->channel = ap.channel;
    }
    if (out_port != NULL)
    {
        /* The caller named a role, so hand back the matching port and nothing
         * else.  Reported as 0 rather than substituted with a default: a wrong-but-
         * plausible port sends the phone to a door that silently drops it, which is
         * far harder to diagnose than an obviously absent one. */
        *out_port = (role == EBADGE_AP_PORT_STREAM) ? ap.stream_port
                    : ap.file_port;
        if (*out_port == 0U)
        {
            EBADGE_WARN1("port_softap: AP=UP but no %s port in the state block",
                         (role == EBADGE_AP_PORT_STREAM) ? "stream" : "file");
        }
    }
    return true;
#else
    (void)out_info; (void)role; (void)out_port;
    return false;
#endif
}
