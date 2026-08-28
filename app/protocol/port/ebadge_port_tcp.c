/**
 * @file    ebadge_port_tcp.c
 * @brief   Data-plane endpoint over the 8711's AT socket commands.
 *
 * The phone's connection still terminates on the 8711 -- there is no lwIP on this
 * chip -- but the 8711 is a network card now rather than a protocol peer:
 *
 *     phone --Wi-Fi/TCP:9000--> 8711FA --[$][SKT][DATA] over SPI--> 8773G (us)
 *
 * So both directions run through this file, which they did not before:
 *
 *  - INBOUND is a raw TCP byte stream.  at_socket hands us slices of it with the
 *    port they arrived on, and we pass them to whoever called listen().  Nothing
 *    is parsed here -- what the bytes mean differs per port (EBXF header plus body
 *    on 9000, bare JPEG on 5004) and that is the session's business.
 *
 *  - OUTBOUND is the 8-byte EBXR verdict, packed here and pushed with
 *    AT+SKTSENDRAW.
 *
 * ---------------------------------------------------------------------------
 * WHAT THE OLD FIRMWARE DID FOR US AND NOW DOES NOT
 * ---------------------------------------------------------------------------
 * It owned both listeners, framed the stream, checked CRCs, and built the EBXR
 * itself from an AT+XFERACK we sent it -- which is why listen() used to be an
 * empty function that recorded a port number for logging, and why the ack was two
 * bytes of meaning rather than a frame.  Three consequences, all of them here:
 *
 *   listen() DOES something.   AT+SKTSERVER, on a link id we choose, because the
 *                              8711 does not echo it back on success.
 *   the ack is OUR frame.      ebxr_pack() into eight bytes, pushed on the seed
 *                              link.  The 120 s window the old firmware gave us to
 *                              answer is gone with the command that had it -- what
 *                              bounds this now is the phone's own patience.
 *   close() DOES something.    AT+SKTDEL.  A listener we forget about is one the
 *                              8711 still has, and the next AT+SKTSERVER on that
 *                              link is refused.
 *
 * ---------------------------------------------------------------------------
 * WHY THE ACK RETRIES AND ALMOST NOTHING ELSE HERE DOES
 * ---------------------------------------------------------------------------
 * at_client queues, so a submit is normally accepted even with a 25 s WLSTARTAP on
 * the wire -- but the queue is four deep and a full one is refused -ENOSPC.  For a
 * query that is harmless: it runs again next tick.  For the verdict it is not,
 * because nothing else will ever send it and a phone that gets no EBXR cannot tell
 * a verify failure from a crash.
 *
 * So a refusal here is retried from a delayed work item, a few times.  The retry
 * does NOT extend to a command the 8711 answered ERROR: that means the connection
 * we wanted to answer is already gone, and asking again cannot bring it back.
 *
 * ---------------------------------------------------------------------------
 * THE ACK ALSO ORDERS THE BLE VERDICT BEHIND IT
 * ---------------------------------------------------------------------------
 * ack() takes a completion callback, and it fires exactly once whatever happens
 * -- delivered, refused, abandoned after retries, superseded, or never staged at
 * all.  xfer_session uses it to hold back the BLE 0x15/0x16 until the data-plane
 * result is out, so the phone cannot be told "done" over BLE while the upload it
 * is still holding open has heard nothing.
 *
 * Every exit path therefore has to settle: a path that returns without firing
 * would leave the session parked in COMPLETING with no deadline of its own,
 * which is a wedge rather than a missed notification.  ctrl_settle() is the one
 * place that fires it, and it clears the callback before invoking it so a
 * teardown reached from inside cannot see a stale one.
 */
#include <stddef.h>
#include <errno.h>
#include <string.h>
#include "ebadge_port_tcp.h"
#include "ebadge_port_softap.h"
#include "../wifi_xfer/ebxf_frame.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#if defined(CONFIG_WIFI_8711)
#include <zephyr/kernel.h>
#include "at_socket.h"
#endif

static ebadge_tcp_listen_t s_listener;
static bool                s_armed;

#if defined(CONFIG_WIFI_8711)

/*----------------------------------------------------------------------------*
 *  Link ids
 *
 *  Ours to choose, and chosen per PORT rather than "the next free one" on
 *  purpose.  A successful AT+SKTSERVER answers a bare OK without echoing the id,
 *  so the only record of which link is which is the one we keep -- and if that
 *  record and the 8711's disagree, a preview stream's bytes are handed to a file
 *  session.  Pinning the mapping makes the disagreement impossible to reach:
 *  whatever we believe about link 1, the 8711 also has 5004 there or has nothing
 *  there at all.
 *
 *  Low ids because the range is [0,20) and nothing else on this chip allocates
 *  from it.
 *----------------------------------------------------------------------------*/
#define TCP_LINK_FILE     0U
#define TCP_LINK_STREAM   1U

/** The seed link of the connection we are serving, or -1.
 *
 *  Kept rather than looked up with at_socket_seed_of() at reply time, because
 *  "newest connection on this server" and "the connection this transfer arrived
 *  on" are the same thing only until a second phone knocks -- and the verdict
 *  belongs to the transfer, not to whoever connected last. */
static int  s_seed = -1;

/*----------------------------------------------------------------------------*
 *  Listener state -- PER PORT, AND ITS LIFETIME IS THE AP'S
 *
 *  Both servers are created when the radio comes up and destroyed when it goes
 *  down, so this is no longer one flag about "the" listener: at any moment either,
 *  both, or neither may exist, independently of which session is armed.
 *
 *  s_open_inflight is separate from s_open because AT+SKTSERVER is asynchronous and
 *  open_servers() may be called again while one is still on the wire (every arm
 *  calls it).  Without it, a second command would be staged for a link the 8711 is
 *  already binding, and the loser is refused -EADDRINUSE -- which is exactly the
 *  stale-server confusion the pinned link ids exist to prevent.
 *----------------------------------------------------------------------------*/
#define TCP_LINK_COUNT    2U

static bool s_open[TCP_LINK_COUNT];
static bool s_open_inflight[TCP_LINK_COUNT];

/** Which link the currently-armed session is listening on, or -1 when idle.
 *
 *  This is what makes a shared pair of listeners safe for two sessions that must
 *  never see each other's bytes: both servers are up, but only the armed link's
 *  data reaches a sink.  Deriving it from s_listener.port at delivery time would
 *  work equally well; keeping it explicit means sock_data() compares two link ids
 *  rather than re-deriving one on every 16 KiB slot. */
static int  s_armed_link = -1;

static uint16_t port_for_link(uint8_t link)
{
    return (link == TCP_LINK_STREAM) ? EBADGE_AP_STREAM_PORT
           : EBADGE_AP_FILE_PORT;
}

static uint8_t link_for_port(uint16_t port)
{
    if (port == EBADGE_AP_STREAM_PORT)
    {
        return TCP_LINK_STREAM;
    }
    /* Everything else on the file link.  Not a warning: EBADGE_AP_FILE_PORT is
     * the only other port any caller passes -- both come from
     * ebadge_port_softap_info() -- so a third value would be a caller bug loud
     * enough to find without one here. */
    return TCP_LINK_FILE;
}

/*----------------------------------------------------------------------------*
 *  at_socket callbacks -- ON THE SPI TRANSPORT THREAD
 *----------------------------------------------------------------------------*/

/**
 * Inbound bytes.  Forwarded verbatim, and the "verbatim" is load-bearing.
 *
 * NOTHING is stripped, buffered or interpreted here.  The file port's stream
 * starts with the phone's 40-byte EBXF header and xfer_session's own accumulator
 * is what parses it -- so a helpful layer here that peeled it off would leave the
 * session parsing the first 40 bytes of the JPEG as a header, silently, and
 * failing the CRC of every transfer with no line pointing at why.
 *
 * The (offset, total, is_last) triple is dropped for the same reason: it describes
 * one [$][SKT][DATA] event, which is an artefact of the 8711's 16 KiB receive
 * buffer rather than anything the phone sent.  A session tracking its own position
 * in the file cannot use it, and a session that trusted it would resynchronise to
 * a boundary that means nothing.
 *
 * CONTEXT: transport thread.  The callback we invoke is the session's, and each
 * one heap-copies and posts to l2_task -- see the on_data contract in the header.
 */
static void sock_data(uint8_t server_link, uint8_t seed_link,
                      const uint8_t *data, uint16_t len,
                      uint32_t offset, uint32_t total, bool is_last, void *user)
{
    (void)offset; (void)total; (void)is_last; (void)user;

    if (!s_armed || s_listener.on_data == NULL)
    {
        /* A session ended between the 8711 accepting these bytes and us seeing
         * them.  Dropped deliberately: there is nobody left with state to put them
         * in, and the alternative is a session resurrected by its own leftovers. */
        return;
    }

    /* Both listeners are up for the whole life of the AP now, so bytes can arrive
     * on a port no session is claiming -- a phone still draining a preview socket
     * while a file transfer is armed, or one that connected to the wrong door.
     * Checking the link is what keeps those out of the armed session's
     * accumulator; before the doors were opened together this could not happen,
     * because the only socket that existed was the armed one. */
    if ((int)server_link != s_armed_link)
    {
        EBADGE_WARN2("port_tcp: %u bytes on link %u which no session is claiming "
                     "-- dropped", (unsigned)len, (unsigned)server_link);
        return;
    }

    /* Learn the seed from data as well as from the connect event.  Belt and
     * braces: an event lost to a slot overrun would otherwise leave the verdict
     * with nowhere to go, and a link that is sending us a file is unambiguously
     * one we may reply on. */
    if (s_seed < 0)
    {
        s_seed = (int)seed_link;
    }

    s_listener.on_data(data, len);
}

/**
 * A connection opened or closed.
 *
 * The open half is also the ONLY "a station is here" edge this stack gets: the
 * Wi-Fi command set emits no association events at all, so a session waiting to
 * be joined is really waiting for this -- see
 * ebadge_port_softap_report_sta_joined(), which is what turns it into the
 * session's callback.
 *
 * CONTEXT: transport thread, so the softap notification is marshalled.  That hop
 * is not optional: the sessions' joined handlers move a state machine and arm
 * deadlines, all of which l2_task owns -- and xfer_session's handler asserts in a
 * comment that it is already there.
 *
 * on_close is NOT marshalled, for the same reason on_data is not: the sessions
 * register a stub that packs the reason and posts it itself.
 */
static void joined_on_l2(void *arg)
{
    (void)arg;
    ebadge_port_softap_report_sta_joined();
}

static void sock_conn(uint8_t server_link, uint8_t seed_link, bool connected,
                      void *user)
{
    (void)user;

    if (connected)
    {
        /* A connection on a port nobody is claiming.  Now reachable in a way it was
         * not before: the doors open when the radio does, so a phone can knock
         * before any offer has been answered -- and the preview door stands open
         * during a file transfer.  Recorded, not acted on: turning it into a joined
         * edge would fire the wrong session's state machine, and taking its seed
         * would point the EBXR verdict at the wrong connection.
         *
         * ebadge_port_tcp_listen() re-checks for an existing connection when a
         * session finally arms, so an early knock is not lost either. */
        if ((int)server_link != s_armed_link)
        {
            EBADGE_LOG2("port_tcp: a phone connected on link %u (port %u) with no "
                        "session armed -- noted",
                        (unsigned)server_link,
                        (unsigned)port_for_link(server_link));
            return;
        }

        s_seed = (int)seed_link;
        EBADGE_LOG1("port_tcp: phone connected (seed=%u)", (unsigned)seed_link);
        (void)ebadge_task_post_call(joined_on_l2, NULL);
        return;
    }

    /* A close on the unclaimed door is equally not ours to report -- but only the
     * seed comparison can tell us that here, because a seed outlives the arm that
     * created it: a session may have been replaced since. */
    if (s_seed != (int)seed_link)
    {
        EBADGE_LOG1("port_tcp: a connection we are not serving closed (seed=%u)",
                    (unsigned)seed_link);
        return;
    }
    s_seed = -1;
    EBADGE_LOG1("port_tcp: phone disconnected (seed=%u)", (unsigned)seed_link);

    if (s_armed && s_listener.on_close != NULL)
    {
        /* Reported as PEER even for a link the 8711 dropped on an error of its
         * own: from the session's side the difference does not change the move --
         * a file that has not arrived is incomplete either way -- and the reason
         * codes it does distinguish (LOCAL, TIMEOUT) are ones it produced itself. */
        s_listener.on_close(EBADGE_TCP_CLOSE_PEER);
    }
}

/**
 * One AT+SKTSERVER has answered.  @p user carries the link id as an integer rather
 * than a pointer: it is a value, and the alternative would be a static per link
 * whose only purpose is to have an address.
 */
static void server_open_done(bool ok, int error_no, void *user)
{
    uint8_t link = (uint8_t)(uintptr_t)user;

    if (link >= TCP_LINK_COUNT)
    {
        return;
    }
    s_open_inflight[link] = false;

    if (ok)
    {
        s_open[link] = true;
        EBADGE_LOG2("port_tcp: listening on port=%u (link %u)",
                    (unsigned)port_for_link(link), (unsigned)link);
        return;
    }

    /* Nothing to tell the phone, and now nothing is waiting on it either -- which is
     * the improvement over doing this at offer time.  The failure lands while the
     * radio is coming up, seconds before any offer, so a session that needs this
     * port is refused synchronously by listen() with -ENETDOWN and reports AP_START
     * up front, instead of the phone being handed a port whose socket silently does
     * not exist.
     *
     * Left un-retried: a second AT+SKTSERVER on a link the first half-bound is
     * answered -EADDRINUSE, which is strictly worse than a clean absence.  The next
     * arm calls open_servers() again, and a BLE reconnect is the recovery. */
    EBADGE_WARN2("port_tcp: SKTSERVER on port=%u failed (error_no=%d) -- that door "
                 "stays shut", (unsigned)port_for_link(link), error_no);
    s_open[link] = false;
}

/** Create one server, unless it exists or is being created. */
static void open_one(uint8_t link)
{
    if (s_open[link] || s_open_inflight[link])
    {
        return;
    }

    /* Static: at_socket copies the struct, but it is handed the pointer, and one
     * shared const for both links is correct because the callbacks route by the
     * server_link they are given rather than by which registration they came
     * through. */
    static const at_socket_cbs_t cbs =
    {
        .on_data = sock_data,
        .on_conn = sock_conn,
        .user    = NULL,
    };

    int rc = at_socket_server_open(link, port_for_link(link), &cbs,
                                   server_open_done, (void *)(uintptr_t)link);

    if (rc != 0)
    {
        /* -ENOSPC is the four-deep queue, which drains; the next arm tries again.
         * Not retried from here on a timer: unlike the EBXR verdict nothing is
         * sequencing behind this, and an offer that needs the port fails loudly at
         * listen() rather than silently. */
        EBADGE_WARN2("port_tcp: SKTSERVER port=%u could not be queued rc=%d",
                     (unsigned)port_for_link(link), rc);
        return;
    }
    s_open_inflight[link] = true;
    EBADGE_LOG2("port_tcp: opening the listener on port=%u (link %u)",
                (unsigned)port_for_link(link), (unsigned)link);
}

#endif /* CONFIG_WIFI_8711 */

void ebadge_port_tcp_init(void)
{
#if defined(CONFIG_WIFI_8711)
    /* at_socket keeps its own s_inited and its own callback registrations, so
     * at_wifi_init() coming first (from port_softap_init) does NOT cover this even
     * though both descend into the same at_client_init().  That shared bottom half
     * is precisely why the omission was invisible: the transport came up, the
     * parser came up, AT+WLSTARTAP worked and the AP really was on air -- and then
     * every AT+SKTSERVER was refused -ENODEV and the session blamed AP_START.
     *
     * Idempotent below, so being called after at_wifi_init() is free. */
    int rc = at_socket_init();

    if (rc != 0)
    {
        /* Same degradation as port_softap: not fatal.  listen() will answer with
         * the same -ENODEV it would have anyway, the sessions map that to
         * XFER_FAIL, and BLE keeps working.  Logged here so the cause is visible
         * at boot rather than inferred from a failed transfer later. */
        EBADGE_WARN1("port_tcp: at_socket_init failed rc=%d -- no data plane", rc);
        return;
    }
    EBADGE_LOG2("port_tcp: 8711 backend up (file:%u on link %u)",
                (unsigned)EBADGE_AP_FILE_PORT, (unsigned)TCP_LINK_FILE);
#else
    EBADGE_WARN("port_tcp: CONFIG_WIFI_8711=n -- no radio, no data plane");
#endif
}

void ebadge_port_tcp_open_servers(void)
{
#if defined(CONFIG_WIFI_8711)
    /* Both doors, every time, because which session arrives first is not knowable
     * here -- and an idempotent call means the arm path can do this unconditionally
     * without knowing what happened on the previous connection. */
    open_one(TCP_LINK_FILE);
    open_one(TCP_LINK_STREAM);
#endif
}

void ebadge_port_tcp_close_servers(void)
{
#if defined(CONFIG_WIFI_8711)
    for (uint8_t link = 0; link < TCP_LINK_COUNT; link++)
    {
        if (!s_open[link])
        {
            continue;
        }
        /* Fire and forget: what matters is that the 8711 stops having the server
         * while the interface it is bound to still exists, not when we hear so.
         * Deleting a server also destroys every connection hanging off it, so the
         * seed goes with it. */
        EBADGE_LOG2("port_tcp: closing the listener on port=%u (link %u)",
                    (unsigned)port_for_link(link), (unsigned)link);
        (void)at_socket_del(link, NULL, NULL);
        s_open[link] = false;
    }
    s_seed       = -1;
    s_armed_link = -1;
#endif
    /* A session sink that outlived its interface would only collect bytes that can
     * no longer arrive, and ack() would stage an EBXR against a dead seed. */
    s_armed      = false;
}

int ebadge_port_tcp_listen(const ebadge_tcp_listen_t *cfg)
{
    if (cfg == NULL)
    {
        return -1;
    }

#if defined(CONFIG_WIFI_8711)
    uint8_t link = link_for_port(cfg->port);

    if (!s_open[link])
    {
        /* No socket on that port.  Either the AP never came up, or this port's
         * AT+SKTSERVER failed while it did -- and both mean the phone has nowhere to
         * connect, which is AP_START to a session.
         *
         * Reported synchronously, which is the whole point of having created the
         * sockets earlier: the answer is known BEFORE the offer is accepted and the
         * 0x13 goes out, rather than arriving as a refused connection afterwards.
         *
         * One more try on the way past, for the case where the open was merely
         * refused a queue slot at arm time.  It cannot help THIS offer -- it is a
         * round trip -- but it makes the App's retry answerable. */
        EBADGE_WARN1("port_tcp: nothing is listening on port=%u -- no door for this "
                     "session", (unsigned)cfg->port);
        open_one(link);
        return -ENETDOWN;
    }

    s_listener   = *cfg;
    s_armed      = true;
    s_armed_link = (int)link;
    s_seed       = -1;

    EBADGE_LOG2("port_tcp: session armed on port=%u (link %u, already listening)",
                (unsigned)cfg->port, (unsigned)link);

    /* A phone may ALREADY be connected: the doors now open when the radio does, so
     * a station can knock during the seconds between the AP coming up and an offer
     * being answered -- and its connect event arrived while no session was armed,
     * which sock_conn() deliberately only noted.  Adopt it here, or the session
     * would sit in WAIT_STA for 60 s waiting for an edge that has already passed.
     *
     * Only for a connection that is still live: at_socket_seed_of() answers -1 once
     * the 8711 has forgotten it. */
    int seed = at_socket_seed_of(link);

    if (seed >= 0)
    {
        s_seed = seed;
        EBADGE_LOG1("port_tcp: a phone was already connected (seed=%d) -> "
                    "delivering the joined edge now", seed);
        (void)ebadge_task_post_call(joined_on_l2, NULL);
    }
    return 0;
#else
    s_listener = *cfg;
    s_armed    = true;
    EBADGE_LOG1("port_tcp: no radio -- port=%d recorded but nothing is listening",
                (int)cfg->port);
    return 0;
#endif
}

#if defined(CONFIG_WIFI_8711)

/*----------------------------------------------------------------------------*
 *  Pending control command, with a bounded retry for a refused submit
 *----------------------------------------------------------------------------*/

/** at_client queues four deep, so a refusal means four commands are already
 *  waiting -- which clears in the time the one in flight takes.  500 ms is short
 *  enough that eight of them stay well inside the phone's patience and long
 *  enough not to spin. */
#define TCP_CTRL_RETRY_MS     500
#define TCP_CTRL_RETRY_MAX    8

typedef enum
{
    CTRL_NONE = 0,
    CTRL_ACK,
    CTRL_STOP,
} ctrl_kind_t;

static struct
{
    ctrl_kind_t kind;
    uint8_t     status;
    uint8_t     reason;
    uint8_t     tries;

    /* Who to tell when this command is finally settled.  Only ever set for an
     * ack: a stop delivers no verdict, so nothing sequences behind it. */
    ebadge_tcp_ack_done_cb_t done;
    void                    *user;
} s_ctrl;

/**
 * Settle the pending command and fire the completion callback exactly once.
 *
 * The callback is cleared BEFORE it is invoked, not after: it is what releases
 * the session to emit its BLE verdict and tear down, and a teardown that staged
 * another command would otherwise find a stale callback still armed here.
 */
static void ctrl_settle(bool ok)
{
    ebadge_tcp_ack_done_cb_t cb   = s_ctrl.done;
    void                    *user = s_ctrl.user;

    s_ctrl.kind = CTRL_NONE;
    s_ctrl.done = NULL;
    s_ctrl.user = NULL;

    if (cb != NULL)
    {
        cb(ok, user);
    }
}

static void ctrl_retry_work(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_ctrl_work, ctrl_retry_work);

/* Fires a completion that could not be staged at all.  Exists so that path can
 * keep the "never synchronously from inside ack()" guarantee without the caller
 * having to know whether staging succeeded. */
static void ctrl_fail_work(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_fail_work, ctrl_fail_work);

/** Fires on the transport thread once the 8711 has answered. */
static void ctrl_done(bool ok, int error_no, void *user)
{
    (void)user;
    if (ok)
    {
        ctrl_settle(true);
        return;
    }
    /* Refused or timed out.  Not retried -- see the header note.  The transfer
     * itself is not failed over this: the file is already stored and the App's
     * authoritative answer is the BLE 0x15/0x16 notify.  Which is exactly why
     * the callback still fires: the session is waiting to send that notify, and
     * a failed ack must release it rather than strand it. */
    EBADGE_WARN1("port_tcp: the 8711 did not accept the transfer result "
                 "(error_no=%d)", error_no);
    ctrl_settle(false);
}

/** Stage whatever is pending.  Returns the submit rc. */
static int ctrl_submit(void)
{
    if (s_seed < 0)
    {
        /* No connection to answer on.  -ENOTCONN rather than a retry: the phone
         * has already gone, and the eight bytes have nowhere to be sent for the
         * rest of this session's life. */
        return -ENOTCONN;
    }

    if (s_ctrl.kind == CTRL_ACK)
    {
        /* Packed here, into a static rather than the stack: at_client stages the
         * bytes asynchronously (AT+SKTSENDRAW first, the payload after the 8711's
         * ">>>"), so a buffer that died with this function would be transmitted
         * from freed stack.  Eight bytes, one at a time by construction -- the
         * supersede path in ctrl_start() settles the previous command before
         * staging another. */
        static uint8_t ebxr[EBXR_LEN];

        ebxr_pack(ebxr, s_ctrl.status, s_ctrl.reason);
        return at_socket_send_raw((uint8_t)s_seed, ebxr, EBXR_LEN,
                                  ctrl_done, NULL);
    }
    if (s_ctrl.kind == CTRL_STOP)
    {
        /* Deleting the SEED, not the server: the point is to cut this phone's
         * stream off, and taking the listener down with it would leave the next
         * offer of the same BLE connection with no door to knock on. */
        return at_socket_del((uint8_t)s_seed, ctrl_done, NULL);
    }
    return 0;
}

/** True for a refusal that clears on its own -- i.e. worth trying again.
 *
 *  -ENOSPC is at_client's queue being full, which drains as commands complete.
 *  -EBUSY is a same-kind command already on the wire (at_socket refuses a second
 *  delete), which likewise ends.  Anything else is structural: a link that is not
 *  a connection, a module that never initialised. */
static bool ctrl_rc_retryable(int rc)
{
    return (rc == -ENOSPC) || (rc == -EBUSY);
}

static void ctrl_retry_work(struct k_work *work)
{
    (void)work;

    if (s_ctrl.kind == CTRL_NONE)
    {
        return;
    }

    int rc = ctrl_submit();
    if (rc == 0)
    {
        return;
    }
    if (ctrl_rc_retryable(rc) && ++s_ctrl.tries < TCP_CTRL_RETRY_MAX)
    {
        (void)k_work_schedule(&s_ctrl_work, K_MSEC(TCP_CTRL_RETRY_MS));
        return;
    }
    EBADGE_WARN2("port_tcp: transfer result abandoned rc=%d after %u tries",
                 rc, (unsigned)s_ctrl.tries);
    ctrl_settle(false);
}

static void ctrl_fail_work(struct k_work *work)
{
    (void)work;
    ctrl_settle(false);
}

/** Common path for both control commands: try now, retry on a refusal. */
static int ctrl_start(ctrl_kind_t kind, uint8_t status, uint8_t reason,
                      ebadge_tcp_ack_done_cb_t done, void *user)
{
    /* A new command supersedes a pending one.  This happens when a session
     * aborts between staging an ack and it going out; the newer intent is the
     * correct one, and two of these must never be in flight together.
     *
     * The superseded one is settled as failed before the new one is staged, so
     * anything sequencing behind it is released rather than forgotten -- and it
     * is settled first so its callback cannot see the new command's state. */
    (void)k_work_cancel_delayable(&s_ctrl_work);
    if (s_ctrl.kind != CTRL_NONE)
    {
        EBADGE_WARN("port_tcp: superseding a pending transfer result");
        ctrl_settle(false);
    }

    s_ctrl.kind   = kind;
    s_ctrl.status = status;
    s_ctrl.reason = reason;
    s_ctrl.tries  = 0U;
    s_ctrl.done   = done;
    s_ctrl.user   = user;

    int rc = ctrl_submit();
    if (rc == 0)
    {
        return 0;
    }
    if (ctrl_rc_retryable(rc))
    {
        EBADGE_LOG1("port_tcp: AT layer refused the result rc=%d, retrying", rc);
        (void)k_work_schedule(&s_ctrl_work, K_MSEC(TCP_CTRL_RETRY_MS));
        return 0;
    }
    /* Staging failed outright.  Cleared without firing: the caller gets the rc
     * synchronously and fires its own completion, which keeps the "never
     * synchronously from inside ack()" guarantee in the header. */
    s_ctrl.kind = CTRL_NONE;
    s_ctrl.done = NULL;
    s_ctrl.user = NULL;
    return rc;
}

#endif /* CONFIG_WIFI_8711 */

int ebadge_port_tcp_ack(bool ok, uint8_t reason,
                        ebadge_tcp_ack_done_cb_t done, void *user)
{
#if defined(CONFIG_WIFI_8711)
    /* Spec sec.10.3 requires reason==0 on success, and both bytes travel into the
     * EBXR verbatim, so normalise here rather than let a caller's stale reason
     * travel alongside a success.
     *
     * EBXR_STATUS_OK is 0x01 and FAILED is 0x00 -- the OPPOSITE polarity to the
     * BLE result codes in V1.3 sec.2.5, deliberately and permanently.  This is the
     * data plane's own frame with its own history; "unifying" the two would change
     * what every shipped phone App reads out of byte 4. */
    uint8_t status = ok ? EBXR_STATUS_OK : EBXR_STATUS_FAILED;
    uint8_t rsn    = ok ? 0U : reason;

    EBADGE_LOG2("port_tcp: -> EBXR status=%u reason=%u",
                (unsigned)status, (unsigned)rsn);

    int rc = ctrl_start(CTRL_ACK, status, rsn, done, user);
    if (rc != 0)
    {
        /* -ENOTCONN when the phone has already dropped the connection, -ENODEV on
         * a board with no 8711.  Deliberately not fatal to the transfer: the file
         * is stored and BLE carries the real answer. */
        EBADGE_WARN1("port_tcp: EBXR could not be staged rc=%d", rc);
        /* Deferred rather than called inline, to honour the header's promise
         * that this never fires from inside ack(). */
        if (done != NULL)
        {
            s_ctrl.done = done;
            s_ctrl.user = user;
            (void)k_work_schedule(&s_fail_work, K_NO_WAIT);
        }
    }
    return 0;
#else
    EBADGE_LOG2("port_tcp: no 8711 link, result (ok=%d reason=%u) dropped",
                (int)ok, (unsigned)reason);
    /* No transport, but the caller is still sequencing behind this -- report the
     * failure rather than leaving it waiting for a link that does not exist.
     * Direct call is acceptable here only because this build has no transport
     * thread to race with; there is no pending command to corrupt. */
    if (done != NULL)
    {
        done(false, user);
    }
    return 0;
#endif
}

int ebadge_port_tcp_abort(void)
{
#if defined(CONFIG_WIFI_8711)
    if (!s_armed || s_seed < 0)
    {
        /* No connection of ours is up, so there is nothing to cut.  Deleting a
         * link anyway could kill one belonging to whatever started after us. */
        return 0;
    }
    EBADGE_LOG("port_tcp: cutting the connection (no EBXR)");
    (void)ctrl_start(CTRL_STOP, 0U, 0U, NULL, NULL);
    return 0;
#else
    return 0;
#endif
}

int ebadge_port_tcp_close(void)
{
#if defined(CONFIG_WIFI_8711)
    /* The LISTENERS SURVIVE THIS, which is the change: they belong to the AP's
     * lifetime now.  Closing them here is what used to make the next transfer on the
     * same BLE connection pay another AT+SKTSERVER -- and, when that command lost its
     * race with the phone's first connect attempt, what made the second transfer
     * fail with AP_START on a radio that was fine.
     *
     * The phone's own connection is left to end on its own too.  Cutting it belongs
     * to abort(), which the sessions call when they mean it; doing it from every
     * cleanup path would destroy the connection an EBXR was just staged against.
     * ebadge_port_softap_shutdown() calls close_servers() when the radio goes. */
    s_seed       = -1;
    s_armed_link = -1;
#else
    if (s_armed)
    {
        EBADGE_LOG("port_tcp: disarmed");
    }
#endif
    /* Cleared last, and it is what makes a late callback harmless: sock_data()
     * and sock_conn() both check it, so bytes the 8711 had already queued are
     * dropped rather than attributed to a session that has ended. */
    s_armed = false;
    return 0;
}

bool ebadge_port_tcp_connected(void)
{
#if defined(CONFIG_WIFI_8711)
    return s_armed && (s_seed >= 0);
#else
    return false;
#endif
}
