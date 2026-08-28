/**
 * @file    at_socket.c
 * @brief   The link table and the event parsing described in at_socket.h.
 *
 * Two halves that meet in one array:
 *
 *   the event half   reads "[$][SKT][EVENT]: A client[link_id:N,seed,tcp,...]
 *                    connected to server[link_id:M]" and writes s_links[N]
 *   the data half    reads a link_id off a [$][SKT][DATA] event and looks up
 *                    s_links[link_id] to find out whose bytes these are
 *
 * The event half is the only source for the seed-to-server mapping, so a parse
 * failure there is counted rather than shrugged off: it turns the following
 * megabyte of data into "unrouted", and the counter is what tells you why.
 */
#if defined(CONFIG_SPI_AT_MASTER)

#include <zephyr/kernel.h>
#include <errno.h>
#include <string.h>

#include "at_socket.h"
#include "../protocol/ebadge_log.h"

/*----------------------------------------------------------------------------*
 *  Event vocabulary
 *
 *  Matched as substrings rather than parsed positionally.  The firmware builds
 *  these with one printf and its shape is fixed, but the fields we need are
 *  identified by their labels in it, and anchoring on labels survives a field
 *  being added between them.
 *----------------------------------------------------------------------------*/
#define EV_PREFIX       "[$][SKT][EVENT]"
#define EV_CLIENT       "A client[link_id:"
#define EV_CONNECTED    "] connected to server[link_id:"
#define EV_DISCONNECTED "] disconnected from server[link_id:"

/*----------------------------------------------------------------------------*
 *  State
 *
 *  Written from at_client's callback threads (events, data) and read from
 *  l2_task (at_socket_seed_of() on the reply path), so the table is guarded.  The
 *  callbacks themselves are invoked OUTSIDE the lock: on_data runs a reassembler
 *  that blocks on l2_task, and holding a mutex across that would put the reply
 *  path behind a file transfer.
 *----------------------------------------------------------------------------*/
typedef struct
{
    at_link_role_t      role;
    uint16_t            port;
    uint8_t             server_link;
    at_socket_data_cb_t on_data;
    at_socket_conn_cb_t on_conn;
    void               *user;
} link_t;

static struct k_mutex     s_lock;
static bool               s_inited;
static link_t             s_links[AT_SOCKET_LINK_MAX];
static at_socket_stats_t  s_stats;

/*  The link an AT+SKTSERVER is being opened for, so its completion can either
 *  confirm the entry or take it back out.  Single-flight at_client means one at a
 *  time, and the reservation is what stops a second open racing onto the same id. */
static struct
{
    bool                pending;
    uint8_t             link;
    at_socket_done_cb_t done;
    void               *user;
} s_open;

/*  Same for a delete: the entry must not be cleared until the 8711 confirms, or a
 *  refused AT+SKTDEL would leave us blind to data still arriving on it. */
static struct
{
    bool                pending;
    uint8_t             link;
    at_socket_done_cb_t done;
    void               *user;
} s_del;

/*----------------------------------------------------------------------------*
 *  Helpers, all called with the lock held
 *----------------------------------------------------------------------------*/

static void link_clear(uint8_t link)
{
    memset(&s_links[link], 0, sizeof(s_links[link]));
}

/** Drop every seed hanging off @p server, as the 8711 does when its server dies. */
static void seeds_clear(uint8_t server)
{
    for (uint8_t i = 0; i < AT_SOCKET_LINK_MAX; i++)
    {
        if (s_links[i].role == AT_LINK_SEED && s_links[i].server_link == server)
        {
            link_clear(i);
        }
    }
}

/*----------------------------------------------------------------------------*
 *  Event parsing
 *----------------------------------------------------------------------------*/

/* Read a decimal number starting at @p s, stopping at the first non-digit.
 * Returns false on no digits or a value past AT_SOCKET_LINK_MAX -- both mean the
 * line is not what we think it is, and guessing would corrupt the routing table. */
static bool parse_link(const char *s, uint8_t *out)
{
    if (s == NULL || *s < '0' || *s > '9')
    {
        return false;
    }

    uint32_t v = 0;

    while (*s >= '0' && *s <= '9')
    {
        v = (v * 10U) + (uint32_t)(*s - '0');
        s++;
        if (v >= AT_SOCKET_LINK_MAX)
        {
            return false;
        }
    }
    *out = (uint8_t)v;
    return true;
}

/**
 * Handle one [$][SKT][EVENT] line.
 *
 * The connect and disconnect lines are identical apart from the middle phrase, so
 * the phrase is what selects the branch -- and it is searched for AFTER the seed
 * id, because the seed's own "link_id:" comes first in both.
 */
static void handle_conn_event(const char *line)
{
    const char *cli = strstr(line, EV_CLIENT);

    if (cli == NULL)
    {
        /* Some other [SKT][EVENT] shape.  Not counted as unparsed: only the
         * client connect/disconnect pair matters here, and the firmware is free
         * to emit others. */
        return;
    }

    uint8_t seed = 0;

    if (!parse_link(cli + sizeof(EV_CLIENT) - 1U, &seed))
    {
        s_stats.events_unparsed++;
        EBADGE_WARN(EB_DIR_FROM_8711 "at_socket: cannot read seed link from event");
        return;
    }

    bool        connected;
    const char *srv = strstr(cli, EV_CONNECTED);

    if (srv != NULL)
    {
        connected = true;
        srv      += sizeof(EV_CONNECTED) - 1U;
    }
    else
    {
        srv = strstr(cli, EV_DISCONNECTED);
        if (srv == NULL)
        {
            s_stats.events_unparsed++;
            EBADGE_WARN(EB_DIR_FROM_8711 "at_socket: client event with neither "
                        "connected nor disconnected");
            return;
        }
        connected = false;
        srv      += sizeof(EV_DISCONNECTED) - 1U;
    }

    uint8_t server = 0;

    if (!parse_link(srv, &server))
    {
        s_stats.events_unparsed++;
        EBADGE_WARN(EB_DIR_FROM_8711 "at_socket: cannot read server link from "
                    "event");
        return;
    }

    at_socket_conn_cb_t cb  = NULL;
    void               *cbu = NULL;
    uint16_t            port = 0;
    bool                known;

    k_mutex_lock(&s_lock, K_FOREVER);
    known = (s_links[server].role == AT_LINK_SERVER);
    if (known)
    {
        cb   = s_links[server].on_conn;
        cbu  = s_links[server].user;
        port = s_links[server].port;

        if (connected)
        {
            /* The seed entry is what makes the following data routable, and the
             * server's callbacks are copied into it rather than looked up through
             * server_link on every chunk: the data path then costs one array read
             * with no second indirection to get wrong. */
            s_links[seed].role        = AT_LINK_SEED;
            s_links[seed].port        = port;
            s_links[seed].server_link = server;
            s_links[seed].on_data     = s_links[server].on_data;
            s_links[seed].on_conn     = s_links[server].on_conn;
            s_links[seed].user        = s_links[server].user;
            s_stats.seeds_connected++;
        }
        else
        {
            link_clear(seed);
            s_stats.seeds_closed++;
        }
    }
    k_mutex_unlock(&s_lock);

    if (!known)
    {
        /* A connection to a listener we never opened, or one whose server entry we
         * have already deleted.  Counted rather than warned per event: an 8711 that
         * kept a listener across our reboot produces one of these per phone
         * connection for as long as it lasts. */
        s_stats.events_unparsed++;
        return;
    }

    EBADGE_LOG("at_socket: seed %u %s server %u (port %u)", (unsigned)seed,
               connected ? "connected to" : "disconnected from",
               (unsigned)server, (unsigned)port);

    if (cb != NULL)
    {
        cb(server, seed, connected, cbu);
    }
}

/** at_client event sink: every "[$]..." line, ours or not. */
static void on_event(const char *line, uint16_t len, void *user)
{
    (void)len;
    (void)user;

    if (strncmp(line, EV_PREFIX, sizeof(EV_PREFIX) - 1U) != 0)
    {
        return;
    }
    handle_conn_event(line);
}

/*----------------------------------------------------------------------------*
 *  Data routing
 *----------------------------------------------------------------------------*/

/** at_client data sink.  CONTEXT: transport thread; @p data dies on return. */
static void on_data(uint8_t link, const uint8_t *data, uint16_t len,
                    uint32_t offset, uint32_t total, bool is_last, void *user)
{
    (void)user;

    if (link >= AT_SOCKET_LINK_MAX)
    {
        s_stats.data_unrouted++;
        return;
    }

    /* The lock covers the table read only.  The callback is invoked after it is
     * released because it reassembles and blocks on l2_task, and a mutex held
     * across that would serialise the reply path behind a file transfer. */
    k_mutex_lock(&s_lock, K_FOREVER);
    at_socket_data_cb_t cb     = s_links[link].on_data;
    void               *cbu    = s_links[link].user;
    uint8_t             server = s_links[link].server_link;
    bool                routable = (s_links[link].role == AT_LINK_SEED);
    k_mutex_unlock(&s_lock);

    if (offset == 0U)
    {
        s_stats.data_events++;
    }
    s_stats.data_bytes += len;

    if (!routable || cb == NULL)
    {
        /* Data on a link with no seed entry, which in practice means its connect
         * event was missed or the connection outlived our table.  Counted rather
         * than logged: at 16 KiB an event a JPEG stream would bury the console. */
        s_stats.data_unrouted++;
        return;
    }

    cb(server, link, data, len, offset, total, is_last, cbu);
}

/*----------------------------------------------------------------------------*
 *  AT+SKTSERVER
 *----------------------------------------------------------------------------*/

static void open_done(at_result_t res, int error_no, void *user)
{
    (void)user;

    bool ok = (res == AT_RES_OK);

    k_mutex_lock(&s_lock, K_FOREVER);
    uint8_t link = s_open.link;

    if (ok)
    {
        s_links[link].role = AT_LINK_SERVER;
        s_stats.servers_opened++;
    }
    else
    {
        /* The reservation is taken back out, callbacks and all.  Leaving it would
         * make a retry on the same id return -EADDRINUSE for a listener that does
         * not exist. */
        link_clear(link);
    }
    at_socket_done_cb_t cb  = s_open.done;
    void               *cbu = s_open.user;
    uint16_t            port = s_links[link].port;

    s_open.pending = false;
    s_open.done    = NULL;
    s_open.user    = NULL;
    k_mutex_unlock(&s_lock);

    if (ok)
    {
        EBADGE_LOG("at_socket: server link %u listening on port %u",
                   (unsigned)link, (unsigned)port);
    }
    else
    {
        /* error_no is worth printing: "socket already in use" and "Socket support
         * not built into this firmware" arrive by the same route. */
        EBADGE_WARN("at_socket: server link %u failed (%s, error_no=%d)",
                    (unsigned)link, at_result_str(res), error_no);
    }

    if (cb != NULL)
    {
        cb(ok, error_no, cbu);
    }
}

int at_socket_server_open(uint8_t link, uint16_t port,
                          const at_socket_cbs_t *cbs,
                          at_socket_done_cb_t done, void *user)
{
    if (!s_inited)
    {
        return -ENODEV;
    }
    if (link >= AT_SOCKET_LINK_MAX || port == 0U ||
        cbs == NULL || cbs->on_data == NULL)
    {
        return -EINVAL;
    }

    k_mutex_lock(&s_lock, K_FOREVER);
    if (s_open.pending || s_links[link].role != AT_LINK_FREE)
    {
        k_mutex_unlock(&s_lock);
        return -EADDRINUSE;
    }

    /* Reserved but not yet AT_LINK_SERVER: the callbacks have to be in the table
     * before the command goes out, because auto_rcv means a phone can connect and
     * push bytes in the same breath as the OK, but the ROLE must not claim a
     * listener exists until the 8711 has said so. */
    s_links[link].port        = port;
    s_links[link].server_link = link;
    s_links[link].on_data     = cbs->on_data;
    s_links[link].on_conn     = cbs->on_conn;
    s_links[link].user        = cbs->user;

    s_open.pending = true;
    s_open.link    = link;
    s_open.done    = done;
    s_open.user    = user;
    k_mutex_unlock(&s_lock);

    /* Exactly six argv, with an empty certificate slot -- the 8711 counts them and
     * rejects anything else.  conn_type 1 is TCP (0 would be UDP), and the
     * trailing 1 is auto_rcv, which is the whole data plane. */
    int rc = at_client_submitf(NULL, open_done, NULL, AT_SOCKET_OPEN_TIMEOUT_MS,
                               "AT+SKTSERVER=%u,1,,%u,1",
                               (unsigned)link, (unsigned)port);

    if (rc != 0)
    {
        k_mutex_lock(&s_lock, K_FOREVER);
        link_clear(link);
        s_open.pending = false;
        s_open.done    = NULL;
        s_open.user    = NULL;
        k_mutex_unlock(&s_lock);
        EBADGE_WARN("at_socket: SKTSERVER submit failed rc=%d", rc);
        return rc;
    }

    EBADGE_LOG("at_socket: opening TCP server link %u port %u (auto_rcv)",
               (unsigned)link, (unsigned)port);
    return 0;
}

/*----------------------------------------------------------------------------*
 *  AT+SKTDEL
 *----------------------------------------------------------------------------*/

static void del_done(at_result_t res, int error_no, void *user)
{
    (void)user;

    bool ok = (res == AT_RES_OK);

    k_mutex_lock(&s_lock, K_FOREVER);
    uint8_t link = s_del.link;
    bool    was_server = (s_links[link].role == AT_LINK_SERVER);

    /* Cleared whether or not the command was accepted.  A refusal almost always
     * means the link was already gone -- the phone closed first, or the server it
     * hung off was deleted -- and continuing to believe in it would route data to
     * a callback whose session has ended. */
    if (was_server)
    {
        seeds_clear(link);
    }
    link_clear(link);

    at_socket_done_cb_t cb  = s_del.done;
    void               *cbu = s_del.user;

    s_del.pending = false;
    s_del.done    = NULL;
    s_del.user    = NULL;
    k_mutex_unlock(&s_lock);

    if (!ok)
    {
        EBADGE_LOG("at_socket: SKTDEL link %u not confirmed (%s, error_no=%d) -- "
                   "forgetting it anyway", (unsigned)link, at_result_str(res),
                   error_no);
    }

    if (cb != NULL)
    {
        cb(ok, error_no, cbu);
    }
}

int at_socket_del(uint8_t link, at_socket_done_cb_t done, void *user)
{
    if (!s_inited)
    {
        return -ENODEV;
    }
    if (link >= AT_SOCKET_LINK_MAX)
    {
        return -EINVAL;
    }

    k_mutex_lock(&s_lock, K_FOREVER);
    if (s_links[link].role == AT_LINK_FREE)
    {
        k_mutex_unlock(&s_lock);
        return -ENOENT;
    }
    if (s_del.pending)
    {
        k_mutex_unlock(&s_lock);
        return -EBUSY;
    }
    s_del.pending = true;
    s_del.link    = link;
    s_del.done    = done;
    s_del.user    = user;
    k_mutex_unlock(&s_lock);

    int rc = at_client_submitf(NULL, del_done, NULL, AT_SOCKET_DEL_TIMEOUT_MS,
                               "AT+SKTDEL=%u", (unsigned)link);

    if (rc != 0)
    {
        k_mutex_lock(&s_lock, K_FOREVER);
        s_del.pending = false;
        s_del.done    = NULL;
        s_del.user    = NULL;
        k_mutex_unlock(&s_lock);
        EBADGE_WARN("at_socket: SKTDEL submit failed rc=%d", rc);
        return rc;
    }

    EBADGE_LOG("at_socket: deleting link %u", (unsigned)link);
    return 0;
}

/*----------------------------------------------------------------------------*
 *  AT+SKTSENDRAW
 *----------------------------------------------------------------------------*/

static at_socket_done_cb_t s_raw_done;
static void               *s_raw_user;

static void raw_done(at_result_t res, int error_no, void *user)
{
    (void)user;

    at_socket_done_cb_t cb  = s_raw_done;
    void               *cbu = s_raw_user;

    s_raw_done = NULL;
    s_raw_user = NULL;

    if (res != AT_RES_OK)
    {
        /* error_no 4 specifically means transparent transmission is unavailable,
         * i.e. the 8711 was built without Host Control Mode -- a build problem
         * rather than a socket problem, and the only place it ever shows up. */
        EBADGE_WARN("at_socket: raw send failed (%s, error_no=%d)",
                    at_result_str(res), error_no);
    }

    if (cb != NULL)
    {
        cb(res == AT_RES_OK, error_no, cbu);
    }
}

int at_socket_send_raw(uint8_t seed, const uint8_t *data, uint16_t len,
                       at_socket_done_cb_t done, void *user)
{
    if (!s_inited)
    {
        return -ENODEV;
    }
    if (seed >= AT_SOCKET_LINK_MAX || data == NULL || len == 0U)
    {
        return -EINVAL;
    }

    k_mutex_lock(&s_lock, K_FOREVER);
    bool live = (s_links[seed].role == AT_LINK_SEED);
    k_mutex_unlock(&s_lock);

    if (!live)
    {
        /* Checked because the alternative is expensive and confusing: the 8711
         * would accept AT+SKTSENDRAW, print ">>>", swallow the payload and only
         * then answer ERROR, several seconds after the session gave up. */
        return -ENOTCONN;
    }

    s_raw_done = done;
    s_raw_user = user;

    int rc = at_client_send_raw(seed, data, len, raw_done, NULL);

    if (rc != 0)
    {
        s_raw_done = NULL;
        s_raw_user = NULL;
    }
    return rc;
}

/*----------------------------------------------------------------------------*
 *  Introspection
 *----------------------------------------------------------------------------*/

bool at_socket_link_get(uint8_t link, at_socket_link_t *out)
{
    if (link >= AT_SOCKET_LINK_MAX || out == NULL)
    {
        return false;
    }
    k_mutex_lock(&s_lock, K_FOREVER);
    out->role        = s_links[link].role;
    out->port        = s_links[link].port;
    out->server_link = s_links[link].server_link;
    k_mutex_unlock(&s_lock);
    return true;
}

int at_socket_seed_of(uint8_t server_link)
{
    int found = -1;

    if (server_link >= AT_SOCKET_LINK_MAX)
    {
        return -1;
    }

    k_mutex_lock(&s_lock, K_FOREVER);
    /* Highest id wins as a stand-in for "newest": the 8711 allocates the lowest
     * free slot, so this is only an approximation -- which is why the header tells
     * callers who care to keep the seed from on_conn. */
    for (uint8_t i = 0; i < AT_SOCKET_LINK_MAX; i++)
    {
        if (s_links[i].role == AT_LINK_SEED &&
            s_links[i].server_link == server_link)
        {
            found = (int)i;
        }
    }
    k_mutex_unlock(&s_lock);
    return found;
}

uint8_t at_socket_seed_count(uint8_t server_link)
{
    uint8_t n = 0;

    if (server_link >= AT_SOCKET_LINK_MAX)
    {
        return 0U;
    }

    k_mutex_lock(&s_lock, K_FOREVER);
    for (uint8_t i = 0; i < AT_SOCKET_LINK_MAX; i++)
    {
        if (s_links[i].role == AT_LINK_SEED &&
            s_links[i].server_link == server_link)
        {
            n++;
        }
    }
    k_mutex_unlock(&s_lock);
    return n;
}

void at_socket_get_stats(at_socket_stats_t *out)
{
    if (out != NULL)
    {
        *out = s_stats;
    }
}

/*----------------------------------------------------------------------------*
 *  Init / reset
 *----------------------------------------------------------------------------*/

void at_socket_reset(void)
{
    k_mutex_lock(&s_lock, K_FOREVER);
    memset(s_links, 0, sizeof(s_links));
    s_open.pending = false;
    s_del.pending  = false;
    k_mutex_unlock(&s_lock);
    EBADGE_LOG("at_socket: link table cleared");
}

/** The 8711 rebooted, so every link we believed in is gone with it.
 *
 *  Registered here rather than left to the port layer because the table is
 *  private: a caller cannot forget links it cannot see, and one stale server
 *  entry is enough to make the next at_socket_server_open() answer -EADDRINUSE
 *  for a listener that does not exist. */
static void on_link_ready(void *user)
{
    (void)user;
    at_socket_reset();
}

int at_socket_init(void)
{
    if (s_inited)
    {
        return 0;
    }

    k_mutex_init(&s_lock);
    memset(s_links, 0, sizeof(s_links));

    int rc = at_client_init();

    if (rc != 0)
    {
        return rc;
    }

    at_client_add_event_cb(on_event, NULL);
    at_client_set_data_cb(on_data, NULL);
    at_client_add_ready_cb(on_link_ready, NULL);

    s_inited = true;
    EBADGE_LOG("at_socket: up (%u links, data events up to %u B)",
               (unsigned)AT_SOCKET_LINK_MAX, (unsigned)AT_SOCKET_DATA_MAX);
    return 0;
}

#endif /* CONFIG_SPI_AT_MASTER */
