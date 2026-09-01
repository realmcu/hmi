/**
 * @file    ebadge_port_softap.c
 * @brief   SoftAP port backed by the RTL8711FA over SPI, standard AT command set.
 *
 * Two jobs, and they are not the same job:
 *
 *   1. Answer "what is the AP and is it usable" synchronously, because the BLE
 *      handlers that ask run on l2_task and an AT round trip is seconds.
 *   2. Turn "a station is here" into a callback, which the Wi-Fi command set does
 *      not offer as an event.
 *
 * ---------------------------------------------------------------------------
 * MOST OF THIS FILE WAS ABOUT NOT KNOWING THINGS
 * ---------------------------------------------------------------------------
 * Against the in-house firmware this module had a freshness rule, a liveness
 * probe, a four-attempt arm ladder with a 15 s spacing, a STARTING branch that
 * waited without re-asking, a data-plane override that outranked the state block,
 * and a join poll -- six mechanisms, and every one of them existed because the
 * answers arrived in a ~1 Hz state block the 8711 pushed and that block could be
 * absent, old, or disagree with reality.  The standard command set removes the
 * question rather than answering it better:
 *
 *   the credentials   are ARGUMENTS to AT+WLSTARTAP.  at_wifi holds the copy we
 *                     wrote before the command went out, so there is nothing to
 *                     discover and nothing to go stale.
 *   the ports         are ours.  ebadge_port_tcp.c creates the listeners with
 *                     AT+SKTSERVER from EBADGE_AP_{FILE,STREAM}_PORT, so a port
 *                     cannot be missing from a description we do not read.
 *   "is it up"        is answered by whether OUR start command returned OK.  The
 *                     8711's at_wlstartap polls its own driver for up to 20 s and
 *                     only prints OK once the running SSID matches the one asked
 *                     for -- so an OK is the radio being on air, not a request
 *                     being accepted, and there is no STARTING state to model.
 *   the joined edge   comes from at_socket, via ebadge_port_tcp.c calling
 *                     ebadge_port_softap_report_sta_joined().  See that function.
 *
 * What is left is: pick credentials, ask once, remember the claim, hand it out.
 *
 * ---------------------------------------------------------------------------
 * WHY THERE IS NO RETRY LADDER
 * ---------------------------------------------------------------------------
 * A start either brings the radio up or reports why, and it has already spent up
 * to 20 s finding out -- internally, inside the one command, with the 8711
 * re-checking every second.  A second WLSTARTAP after that failed would ask the
 * same question of the same driver and occupy the single-flight AT layer for
 * another 25 s while doing it.  The retry that is actually useful is the App's:
 * an offer answered NOT_READY is retried, and by then this module's answer has
 * changed or the radio is genuinely broken.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ebadge_port_softap.h"
#include "ebadge_port_tcp.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#if defined(CONFIG_WIFI_8711)
#include "at_wifi.h"
#include "hmi_ble_gap_init.h"   /* the BD address the SSID is derived from */
#endif

#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
/** How long after a transfer ends before the radio goes down.
 *
 *  Not a guess at "long enough to be polite" -- it is sized off what is still in
 *  flight when the caller asks.  The EBXR verdict is eight bytes we push over the
 *  phone's own TCP connection and the 0x15/0x16 notify has only just been queued
 *  on the BLE link.  Three seconds clears both with room to spare, and it is short
 *  enough that a phone which is genuinely done sees the hotspot disappear rather
 *  than linger.
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
 *  every other deadline in this stack, and "due at t=0" stays representable across
 *  an uptime wrap.  Touched only on l2_task. */
static bool                          s_idle_down_pending;
static uint32_t                      s_idle_down_at_ms;
#endif /* EBADGE_SOFTAP_PER_TRANSFER_LIFETIME */

#if defined(CONFIG_WIFI_8711)

/*----------------------------------------------------------------------------*
 *  The credentials
 *
 *  Derived from the BLE address rather than random or fixed, and each of those
 *  three choices is a different bug:
 *
 *    fixed   two badges in one room advertise the same SSID, and a phone joins
 *            whichever is louder.  It then uploads somebody else's wallpaper.
 *    random  a re-arm after a reboot offers a different network from the one the
 *            App may still have a saved profile for, and the badge cannot tell
 *            the user what it changed to except through the 0x13 it is trying to
 *            get answered.
 *    derived stable across reboots, unique per device, and derivable from
 *            something the App already saw during the BLE connection.
 *
 *  The App is not required to derive anything, though -- it reads both fields out
 *  of the 0x13 AP_INFO -- so this only has to be stable and unique, not guessable.
 *----------------------------------------------------------------------------*/

/** Fixed rather than left to the 8711 (channel 0 omits the key and lets it
 *  choose).  Choosing means the number reported in 0x13 is the number the radio is
 *  on: an omitted key would leave us reporting 0, and the only way to learn what
 *  it picked would be to parse it back out of AT+WLSTATE.  6 is the middle
 *  non-overlapping 2.4 GHz channel. */
#define SOFTAP_CHANNEL      6U

static char s_ssid[AT_WIFI_SSID_MAX];
static char s_pw[AT_WIFI_PW_MAX];

/**
 * Fill s_ssid / s_pw, once.
 *
 * Deliberately tolerant of a BD address that cannot be read: that happens when
 * the BT stack has not finished initialising, and refusing to have an AP at all
 * over it would turn a startup-order detail into "file transfer does not work on
 * this unit".  The fallback names itself in the SSID so the log and the phone's
 * network list both show which case happened.
 */
static void creds_build(void)
{
    if (s_ssid[0] != '\0')
    {
        return;
    }

    uint8_t bd[6];

    if (hmi_ble_gap_get_local_addr(bd))
    {
        /* bd[0] is the LSB, so bd[2..0] are the three low-order octets -- the
         * half of the address that differs between units of the same batch. */
        (void)snprintf(s_ssid, sizeof(s_ssid), "eBadge-%02X%02X%02X",
                       bd[2], bd[1], bd[0]);
        /* All six octets, so the password does not follow from the SSID alone.
         * 12 hex digits plus the prefix is 14 characters, inside the 8..64 WPA2
         * range the 8711 enforces. */
        (void)snprintf(s_pw, sizeof(s_pw), "eb%02X%02X%02X%02X%02X%02X",
                       bd[5], bd[4], bd[3], bd[2], bd[1], bd[0]);
        EBADGE_LOG1("port_softap: credentials from BD addr, ssid=\"%s\"", s_ssid);
        return;
    }

    (void)snprintf(s_ssid, sizeof(s_ssid), "eBadge-NOMAC");
    (void)snprintf(s_pw, sizeof(s_pw), "eBadge2024");
    EBADGE_WARN("port_softap: BD addr unavailable -- using the fallback SSID");
}

/*----------------------------------------------------------------------------*
 *  Bring-up
 *----------------------------------------------------------------------------*/

/** AT+WLSTARTAP has answered.  Runs on an at_client callback thread.
 *
 *  THE LISTENERS GO UP HERE, and this is the whole point of taking a completion
 *  callback where we used to pass NULL.  A TCP server is only reachable while the
 *  interface it is bound to exists, so "the radio is on air" is exactly the moment
 *  both doors can be opened -- and an OK from this command means precisely that
 *  (the 8711 polls its own driver for up to 20 s and prints OK only once the running
 *  SSID matches).
 *
 *  They used to be created by the offer handler instead, which put an AT round trip
 *  between telling the phone ACCEPT and telling it where to connect.  Doing it here
 *  moves both AT+SKTSERVER commands seconds earlier, off the offer's critical path,
 *  and onto a link that is idle by construction: nothing else is in flight, because
 *  the 25 s command that just finished was holding the single-flight layer.
 *
 *  CONTEXT: an at_client callback thread, NOT l2_task.  Safe as it stands because
 *  everything it touches (the per-link server flags in port_tcp) is written only
 *  from here and from the shutdown path, and it puts nothing into a session's state
 *  machine -- the joined edge still arrives via sock_conn(), which marshals. */
static void start_done(bool ok, int error_no, void *user)
{
    (void)user;

    if (!ok)
    {
        /* at_wifi has logged the shape of it and left the AP marked down, so every
         * reader here already answers "not up" and offers get NOT_READY.  No
         * listeners: there is no interface to bind them to. */
        EBADGE_LOG1("port_softap: the AP did not come up (error_no=%d) -- no "
                    "listeners", error_no);
        return;
    }

    EBADGE_LOG("port_softap: AP is on air -> opening the TCP listener now");
    ebadge_port_tcp_open_servers();
}

/** Runs on l2_task, from ebadge_port_softap_arm().
 *
 *  Also reached synchronously from ebadge_port_softap_start() under the
 *  per-transfer arrangement, which already runs on l2_task -- hence the @p why
 *  text: the two callers are asking for the same thing for different reasons, and
 *  a log line that always said "BLE connected" would misattribute the other. */
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

    at_wifi_ap_t ap;

    at_wifi_ap_get(&ap);

    if (ap.up)
    {
        /* Nothing to ask.  Spending a 25 s WLSTARTAP to be told what we already
         * recorded would only occupy the single-flight layer.
         *
         * The doors are still asked for, because this is the path a re-arm takes and
         * the radio being up does not prove both servers are: one of them may have
         * lost its AT+SKTSERVER to a full queue on the previous arm.  Idempotent, so
         * the common case costs nothing. */
        EBADGE_LOG1("port_softap: %s, AP already up", why);
        ebadge_port_tcp_open_servers();
        return;
    }
    if (ap.starting)
    {
        /* Load-bearing rather than an optimisation.  An App handed NOT_READY
         * retries about once a second, and a WLSTARTAP holds the single-flight AT
         * layer for up to ~25 s -- so without this a dozen of them would queue
         * behind the one already doing the work, each arriving after the radio was
         * up and asking for it again.
         *
         * No doors here: the start already on the wire carries a start_done that
         * opens them when it lands. */
        EBADGE_LOG1("port_softap: %s, AP bring-up already in flight", why);
        return;
    }

    creds_build();

    /* The completion callback exists for ONE job: open the TCP listeners the instant
     * the radio is on air -- see start_done().  Nothing else is decided on the
     * answer; at_wifi logs both outcomes and holds the flag every reader here
     * consults, and a start that failed is not retried from this side (see WHY THERE
     * IS NO RETRY LADDER in the file header). */
    int rc = at_wifi_ap_start(s_ssid, s_pw, SOFTAP_CHANNEL, start_done, NULL);

    if (rc != 0)
    {
        EBADGE_WARN2("port_softap: %s, but WLSTARTAP submit failed rc=%d", why, rc);
        return;
    }
    EBADGE_LOG2("port_softap: %s -> starting the AP ssid=\"%s\" (may take >20 s)",
                why, s_ssid);
}

static void arm_on_l2(void *arg)
{
    (void)arg;
    arm_begin("BLE connected");
}

/*----------------------------------------------------------------------------*
 *  AP shutdown
 *
 *  One stop at a time.  The flag is written on l2_task (shutdown) and cleared on
 *  an at_client callback thread (stop_done), which is a plain bool store from each
 *  side with no read-modify-write -- and the only consequence of losing the race is
 *  one redundant AT command, which costs a round trip and changes nothing.  A mutex
 *  for that would be more machinery than the risk.
 *----------------------------------------------------------------------------*/
static bool s_stop_inflight;

/** AT-layer completion.  Nothing to decide: at_wifi has already marked the AP down
 *  whatever came back, because AT+WLSTOPAP has no failure path and a non-OK result
 *  therefore means the link broke rather than that the radio survived. */
static void stop_done(bool ok, int error_no, void *user)
{
    (void)error_no;
    (void)user;
    s_stop_inflight = false;
    if (!ok)
    {
        /* at_wifi has already logged the shape of the failure. */
        EBADGE_LOG("port_softap: AP stop not confirmed -- treated as down anyway");
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

    at_wifi_ap_t ap;

    at_wifi_ap_get(&ap);
    if (ap.starting)
    {
        /* A bring-up is on the wire.  Stopping the AP we are in the middle of
         * asking for would queue the two commands against each other on a
         * single-flight link for no benefit -- and something asked for it, so it is
         * wanted. */
        EBADGE_LOG("port_softap: idle-down due but a bring-up is in flight -- "
                   "skipped");
        return;
    }

    EBADGE_LOG1("port_softap: transfer finished %u ms ago and nothing claimed the "
                "AP -> stopping it", (unsigned)SOFTAP_IDLE_DOWN_MS);
    ebadge_port_softap_shutdown();
}

/*----------------------------------------------------------------------------*
 *  Tick -- on l2_task
 *
 *  Registered ONLY under the per-transfer arrangement, because that is the only
 *  thing left with a deadline of its own.  The default arrangement has no periodic
 *  work at all: the credentials do not expire, the AP flag changes on command
 *  completions rather than on a beat, and the joined edge arrives as a socket
 *  event.  Registering a do-nothing sink would spend one of EBADGE_TICK_SINKS,
 *  which is a fixed array with one slot spare.
 *----------------------------------------------------------------------------*/
static void on_tick(uint32_t now_ms)
{
    idle_down_check(now_ms);
}
#endif /* EBADGE_SOFTAP_PER_TRANSFER_LIFETIME */

#endif /* CONFIG_WIFI_8711 */

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
void ebadge_port_softap_init(void)
{
#if defined(CONFIG_WIFI_8711)
    int rc = at_wifi_init();

    if (rc != 0)
    {
        /* The transport did not come up.  Not fatal to the rest of the stack: BLE
         * still works, and every entry point here degrades to a refusal rather than
         * a fault, so the badge answers NOT_READY instead of crashing. */
        EBADGE_WARN1("port_softap: at_wifi_init failed rc=%d -- AP unavailable", rc);
        return;
    }

#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
    ebadge_task_set_tick(on_tick);
#endif

    /* Nothing on the wire from here.  The radio goes up when a phone connects, not
     * when we boot -- an AP nobody is going to join is a beacon and a current
     * draw. */
    EBADGE_LOG2("port_softap: 8711 backend, we own the AP (ch=%u, tcp:%u)",
                (unsigned)SOFTAP_CHANNEL, (unsigned)EBADGE_AP_FILE_PORT);
#else
    EBADGE_WARN("port_softap: CONFIG_WIFI_8711=n -- no radio, AP unavailable");
#endif
}

void ebadge_port_softap_arm(void)
{
#if defined(CONFIG_WIFI_8711)
    /* Marshalled onto l2_task rather than acted on here: the caller is the GAP
     * dispatcher on the BT stack thread, and the state below is read and written
     * from l2_task.  post_call is the same hop the disconnect path already uses for
     * the session aborts.
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

    if (!ebadge_port_softap_info(out_info, out_port))
    {
        /* Do NOT pretend success: the session would emit a 0x13 full of zeroes and
         * the phone would try to join a network called "".  -EAGAIN maps to
         * NOT_READY on the wire, which is the truth.
         *
         * Nothing waits for it here -- l2_task must not block -- so this offer
         * still fails.  What makes that acceptable is that a bring-up is bounded:
         * the App's retry lands after it, not during it forever. */
        EBADGE_WARN("port_softap: AP not up yet -> EAGAIN (App should retry)");
#if defined(CONFIG_WIFI_8711) && EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
        /* And make the retry answerable.  Under this arrangement the radio does not
         * stay up for the whole BLE connection -- a finished transfer schedules it
         * down -- so "down" here is a routine mid-connection state, and without this
         * the second transfer of a connection would collect AP_START forever.
         *
         * Deliberately absent from the default arrangement: there the AP is armed on
         * the BLE connect and lowered on the disconnect, so a "down" seen here is a
         * bring-up still running or a fault, and re-arming a fault on every offer
         * would only spend 25 s commands on a single-flight link.
         *
         * arm_begin() is a no-op while a bring-up is in flight, so an App that
         * retries once a second does not queue a second one. */
        arm_begin("session needs the AP");
#endif
        return -EAGAIN;
    }

    s_joined_cb       = joined_cb;
    s_joined_reported = false;
    s_running         = true;

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

void ebadge_port_softap_report_sta_joined(void)
{
    if (!s_running || s_joined_cb == NULL || s_joined_reported)
    {
        /* Not an error.  A phone that opens a second TCP connection, or one that
         * connects between a session releasing the AP and the next claiming it,
         * both land here -- and neither is an edge anybody is waiting for. */
        return;
    }

    /* Latched before the call, not after: the callback runs a session state machine
     * that can come back through this module, and a second edge would resurrect a
     * transfer that has already moved on. */
    s_joined_reported = true;

    ebadge_softap_sta_joined_cb_t cb = s_joined_cb;

    EBADGE_LOG("port_softap: a phone opened a TCP connection -> notifying session");
    cb();
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
     * keeping: either one is on the wire or one could not be staged, and the
     * schedule would only fire a duplicate. */
    s_idle_down_pending = false;
#endif

    if (s_stop_inflight)
    {
        /* One BLE disconnect is one stop, but nothing stops the GAP layer reporting
         * a disconnect twice, and the second submit would only queue behind the
         * first on a single-flight link.  Collapsing them costs nothing: the command
         * has no arguments, so the one in flight is the same one. */
        return;
    }

    /* Drop BOTH listeners FIRST, and this order is the reason it is done here at all.
     * Stopping the radio destroys the network interface the AT+SKTSERVER sockets are
     * bound to, and at_socket's link table would go on believing in them -- so the
     * next AT+SKTSERVER would be refused -EADDRINUSE for a server the 8711 no longer
     * has, on a link id it would then never be able to reuse.  Deleting while the
     * interface still exists is the only ordering where the 8711 and our table agree
     * afterwards.
     *
     * close_servers() and not close(): the sockets belong to the radio's lifetime now
     * rather than a session's, so this -- the one place the radio goes away -- is the
     * only place they are destroyed.  close() merely disarms a session's sink and
     * would leave both servers up over a vanished interface. */
    ebadge_port_tcp_close_servers();

    int rc = at_wifi_ap_stop(stop_done, NULL);

    if (rc != 0)
    {
        /* Not retried, deliberately.  The AP being left up is a wasted beacon, not
         * a broken transfer, and the next arm re-raises it regardless -- so there is
         * no state to repair and nothing a retry would protect. */
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
                             uint16_t *out_port)
{
#if defined(CONFIG_WIFI_8711)
    at_wifi_ap_t ap;

    /* NOTHING is put on the wire here, and that is the point: this runs on l2_task
     * from a BLE handler that must answer in milliseconds, while the cheapest AT
     * round trip is orders of magnitude slower.
     *
     * There is also nothing to verify against.  What at_wifi holds is not a cached
     * approximation of what the 8711 would say if asked -- it is the record of what
     * we told the 8711 to be, and of whether the command saying so returned OK.
     * AT+WLSTATE could confirm the radio is in AP mode but it carries no port and no
     * credentials, so it could not correct anything here even if it were free. */
    at_wifi_ap_get(&ap);

    if (!ap.up)
    {
        /* Down, or a bring-up still running.  Reported as one answer because the
         * caller's move is the same for both: NOT_READY, and let the App retry.
         * Distinguishing them would let a handler decide to wait, and a handler on
         * l2_task must not wait for a 25 s command. */
        return false;
    }

    if (ap.ssid[0] == '\0')
    {
        /* up with no SSID cannot happen -- at_wifi writes the credentials before it
         * submits and only sets `up` on OK -- so this is a corrupted struct rather
         * than a state.  Refused rather than reported as success, because the phone
         * cannot look for a nameless network. */
        EBADGE_WARN("port_softap: AP is up but the SSID is empty");
        return false;
    }

    if (out_info != NULL)
    {
        memset(out_info, 0, sizeof(*out_info));
        /* Sized from the destination, not the source: at_wifi's buffers are the
         * same width by construction (AT_WIFI_SSID_MAX matches ssid[33]) but the
         * password is not -- 65 there against 64 here -- and the NUL is preserved
         * by the memset above. */
        memcpy(out_info->ssid, ap.ssid, sizeof(out_info->ssid) - 1U);
        memcpy(out_info->password, ap.password, sizeof(out_info->password) - 1U);
        out_info->channel = ap.channel;
        out_info->ip      = EBADGE_AP_IPV4;
    }
    if (out_port != NULL)
    {
        /* A constant, not a lookup.  The listener on this port is created by
         * ebadge_port_tcp.c from the same symbol, so a phone told 9000 is told the
         * port a socket is actually bound to -- which is what the old
         * discovered-port arrangement could not guarantee and warned about.
         *
         * One port for both sessions, so there is nothing to choose here: file
         * transfer and preview cannot be in flight together (each offer refuses with
         * BUSY while the other session is non-IDLE), and what the bytes mean is
         * decided by the armed session rather than by the door. */
        *out_port = EBADGE_AP_FILE_PORT;
    }
    return true;
#else
    (void)out_info; (void)out_port;
    return false;
#endif
}
