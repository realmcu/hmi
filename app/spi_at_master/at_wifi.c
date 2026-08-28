/**
 * @file    at_wifi.c
 * @brief   AT+WL* wrappers plus the cached AP description, as described in
 *          at_wifi.h.
 *
 * Small, because the hard parts moved elsewhere.  There is no retry ladder (a
 * start either brings the radio up or reports why, and it has already spent 20 s
 * finding out), no freshness rule (nothing pushes state at us to go stale), and no
 * join detection (the firmware emits no association events -- at_socket owns that
 * edge now).
 *
 * What is left is one snprintf, one line-sink parser for human-readable text, and
 * a mutex around a struct that l2_task reads while at_client's threads write it.
 */
#if defined(CONFIG_SPI_AT_MASTER)

#include <zephyr/kernel.h>
#include <errno.h>
#include <string.h>

#include "at_wifi.h"
#include "../protocol/ebadge_log.h"

/*----------------------------------------------------------------------------*
 *  Wire strings
 *
 *  The WLSTATE keys carry their exact leading-space padding, because that padding
 *  is part of what the 8711 prints and matching on the trimmed key would also
 *  match a password whose value happened to contain "MODE => ".  Widths come from
 *  atcmd_wifi.c: "      MODE => %s" (6 spaces), "Client Num: %d" (none).
 *----------------------------------------------------------------------------*/
#define WLSTATE_MODE_AP     "      MODE => AP"
#define WLSTATE_CLIENTS     "Client Num:"
#define WLSTATE_RUNNING_TAIL " Status: Running"

/*----------------------------------------------------------------------------*
 *  State
 *
 *  s_ap is written from at_client's callback threads and read from l2_task, so it
 *  is copied under a mutex rather than read field by field: a caller that saw a
 *  new SSID next to an old password would build a 0x13 offer no phone can use.
 *----------------------------------------------------------------------------*/
static struct k_mutex  s_lock;
static bool            s_inited;
static at_wifi_ap_t    s_ap;

/*  The pending start/stop's caller, kept here rather than in the at_client user
 *  pointer because the cached AP has to be updated before the caller's callback
 *  runs -- a callback that immediately calls at_wifi_ap_up() must see the new
 *  answer, not the one from before the command it is being told about. */
static at_wifi_done_cb_t s_done_cb;
static void             *s_done_user;

/*  Same for a query in flight: its parse accumulates across several line
 *  callbacks, so it cannot live on the submitter's stack. */
static at_wifi_state_cb_t s_state_cb;
static void              *s_state_user;
static at_wifi_state_t    s_state;

/*----------------------------------------------------------------------------*
 *  SoftAP: start
 *----------------------------------------------------------------------------*/

static void start_done(at_result_t res, int error_no, void *user)
{
    (void)user;

    bool ok = (res == AT_RES_OK);

    k_mutex_lock(&s_lock, K_FOREVER);
    s_ap.starting = false;
    s_ap.up       = ok;
    if (!ok)
    {
        /* The credentials stay in the struct on failure.  They describe what was
         * asked for, which is what a log line and a retry both want; `up` is the
         * field that says whether they are on the air. */
    }
    at_wifi_done_cb_t cb   = s_done_cb;
    void             *cbu  = s_done_user;
    s_done_cb   = NULL;
    s_done_user = NULL;
    k_mutex_unlock(&s_lock);

    if (ok)
    {
        EBADGE_LOG("at_wifi: SoftAP is up");
    }
    else
    {
        /* Named rather than numbered where we can: a start that polled for 20 s
         * without seeing its own SSID is a radio problem, while a rejection
         * arriving immediately is an argument problem, and both come back here as
         * "ERROR: <n>". */
        EBADGE_WARN2("at_wifi: SoftAP start failed (%s, error_no=%d)",
                     at_result_str(res), error_no);
    }

    if (cb != NULL)
    {
        cb(ok, error_no, cbu);
    }
}

int at_wifi_ap_start(const char *ssid, const char *password, uint8_t channel,
                     at_wifi_done_cb_t done, void *user)
{
    if (!s_inited)
    {
        return -ENODEV;
    }
    if (ssid == NULL || password == NULL)
    {
        return -EINVAL;
    }

    size_t ssid_len = strlen(ssid);
    size_t pw_len   = strlen(password);

    /* Checked here as well as on the 8711 because the failure is otherwise 20 s
     * away and reported as a generic ERROR: a password of 7 characters is
     * indistinguishable, in the reply, from a radio that would not come up. */
    if (ssid_len == 0U || ssid_len > (AT_WIFI_SSID_MAX - 1U))
    {
        return -EINVAL;
    }
    if (pw_len < 8U || pw_len > (AT_WIFI_PW_MAX - 1U))
    {
        return -EINVAL;
    }
    if (channel > 14U)
    {
        return -EINVAL;
    }

    k_mutex_lock(&s_lock, K_FOREVER);
    if (s_ap.starting)
    {
        k_mutex_unlock(&s_lock);
        return -EALREADY;
    }

    /* Recorded BEFORE the command goes out, and that ordering is the whole design:
     * this struct is the authoritative copy of what the AP is, so it must already
     * hold the right credentials by the time anything can observe `up`. */
    memset(&s_ap, 0, sizeof(s_ap));
    memcpy(s_ap.ssid, ssid, ssid_len);
    memcpy(s_ap.password, password, pw_len);
    s_ap.channel  = channel;
    s_ap.starting = true;
    s_ap.up       = false;
    s_done_cb     = done;
    s_done_user   = user;
    k_mutex_unlock(&s_lock);

    int rc;

    if (channel != 0U)
    {
        rc = at_client_submitf(NULL, start_done, NULL,
                               AT_WIFI_AP_START_TIMEOUT_MS,
                               "AT+WLSTARTAP=ssid,%s,pw,%s,sec,wpa2,ch,%u",
                               ssid, password, (unsigned)channel);
    }
    else
    {
        rc = at_client_submitf(NULL, start_done, NULL,
                               AT_WIFI_AP_START_TIMEOUT_MS,
                               "AT+WLSTARTAP=ssid,%s,pw,%s,sec,wpa2",
                               ssid, password);
    }

    if (rc != 0)
    {
        k_mutex_lock(&s_lock, K_FOREVER);
        s_ap.starting = false;
        s_done_cb     = NULL;
        s_done_user   = NULL;
        k_mutex_unlock(&s_lock);
        EBADGE_WARN1("at_wifi: WLSTARTAP submit failed rc=%d", rc);
        return rc;
    }

    /* The password is deliberately absent from the log even though it is on the
     * wire: this line is the one that gets pasted into bug reports. */
    EBADGE_LOG2("at_wifi: starting SoftAP ssid=\"%s\" ch=%u (may take >20 s)",
                s_ap.ssid, (unsigned)channel);
    return 0;
}

/*----------------------------------------------------------------------------*
 *  SoftAP: stop
 *----------------------------------------------------------------------------*/

static void stop_done(at_result_t res, int error_no, void *user)
{
    (void)user;

    k_mutex_lock(&s_lock, K_FOREVER);
    /* Marked down whatever came back.  at_wlstopap has no failure path -- it
     * deinits DHCP, calls wifi_stop_ap() and prints OK unconditionally -- so a
     * non-OK result means the AT link broke, not that the radio is still serving.
     * Believing it were still up would leave us handing out an SSID nobody can
     * join. */
    s_ap.up       = false;
    s_ap.starting = false;
    at_wifi_done_cb_t cb  = s_done_cb;
    void             *cbu = s_done_user;
    s_done_cb   = NULL;
    s_done_user = NULL;
    k_mutex_unlock(&s_lock);

    if (res != AT_RES_OK)
    {
        EBADGE_WARN2("at_wifi: WLSTOPAP did not confirm (%s, error_no=%d) -- "
                     "treating the AP as down", at_result_str(res), error_no);
    }
    else
    {
        EBADGE_LOG("at_wifi: SoftAP is down");
    }

    if (cb != NULL)
    {
        cb(res == AT_RES_OK, error_no, cbu);
    }
}

int at_wifi_ap_stop(at_wifi_done_cb_t done, void *user)
{
    if (!s_inited)
    {
        return -ENODEV;
    }

    k_mutex_lock(&s_lock, K_FOREVER);
    s_done_cb   = done;
    s_done_user = user;
    k_mutex_unlock(&s_lock);

    int rc = at_client_submit("AT+WLSTOPAP", NULL, stop_done, NULL,
                              AT_WIFI_AP_STOP_TIMEOUT_MS);

    if (rc != 0)
    {
        k_mutex_lock(&s_lock, K_FOREVER);
        s_done_cb   = NULL;
        s_done_user = NULL;
        k_mutex_unlock(&s_lock);
        EBADGE_WARN1("at_wifi: WLSTOPAP submit failed rc=%d", rc);
        return rc;
    }

    EBADGE_LOG("at_wifi: stopping SoftAP");
    return 0;
}

/*----------------------------------------------------------------------------*
 *  AT+WLSTATE
 *----------------------------------------------------------------------------*/

static void state_line(const char *line, uint16_t len, void *user)
{
    (void)user;

    if (strncmp(line, WLSTATE_MODE_AP, sizeof(WLSTATE_MODE_AP) - 1U) == 0)
    {
        s_state.mode_ap = true;
        return;
    }
    if (strncmp(line, WLSTATE_CLIENTS, sizeof(WLSTATE_CLIENTS) - 1U) == 0)
    {
        const char *p = line + sizeof(WLSTATE_CLIENTS) - 1U;

        while (*p == ' ')
        {
            p++;
        }

        unsigned n = 0;

        while (*p >= '0' && *p <= '9')
        {
            n = (n * 10U) + (unsigned)(*p - '0');
            p++;
        }
        s_state.clients = (uint8_t)((n > 255U) ? 255U : n);
        return;
    }

    /* "WLAN0 Status: Running" -- matched on the tail rather than the whole line
     * because the interface index varies.  The other spelling the firmware prints
     * is "Status: Disabled", which simply leaves the flag false. */
    if (len > (sizeof(WLSTATE_RUNNING_TAIL) - 1U) &&
        strstr(line, WLSTATE_RUNNING_TAIL) != NULL)
    {
        s_state.running = true;
    }
}

static void state_done(at_result_t res, int error_no, void *user)
{
    (void)user;

    at_wifi_state_t   snap = s_state;
    at_wifi_state_cb_t cb  = s_state_cb;
    void              *cbu = s_state_user;

    s_state_cb   = NULL;
    s_state_user = NULL;

    if (res != AT_RES_OK)
    {
        EBADGE_WARN2("at_wifi: WLSTATE failed (%s, error_no=%d)",
                     at_result_str(res), error_no);
    }
    else
    {
        EBADGE_LOG2("at_wifi: WLSTATE mode_ap=%d clients=%u",
                    (int)snap.mode_ap, (unsigned)snap.clients);
    }

    if (cb != NULL)
    {
        cb(res == AT_RES_OK, &snap, cbu);
    }
}

int at_wifi_ap_query(at_wifi_state_cb_t done, void *user)
{
    if (!s_inited)
    {
        return -ENODEV;
    }
    if (s_state_cb != NULL)
    {
        /* One parse at a time.  at_client is single-flight anyway, so a second
         * query would be queued behind this one and its lines would land in the
         * same accumulator -- refusing is clearer than merging two answers. */
        return -EBUSY;
    }

    memset(&s_state, 0, sizeof(s_state));
    s_state_cb   = done;
    s_state_user = user;

    int rc = at_client_submit("AT+WLSTATE", state_line, state_done, NULL,
                              AT_WIFI_STATE_TIMEOUT_MS);

    if (rc != 0)
    {
        s_state_cb   = NULL;
        s_state_user = NULL;
        EBADGE_WARN1("at_wifi: WLSTATE submit failed rc=%d", rc);
    }
    return rc;
}

/*----------------------------------------------------------------------------*
 *  Readers
 *----------------------------------------------------------------------------*/

void at_wifi_ap_get(at_wifi_ap_t *out)
{
    if (out == NULL)
    {
        return;
    }
    k_mutex_lock(&s_lock, K_FOREVER);
    *out = s_ap;
    k_mutex_unlock(&s_lock);
}

bool at_wifi_ap_up(void)
{
    k_mutex_lock(&s_lock, K_FOREVER);
    bool up = s_ap.up;
    k_mutex_unlock(&s_lock);
    return up;
}

/*----------------------------------------------------------------------------*
 *  Init
 *----------------------------------------------------------------------------*/

/** The 8711 rebooted, so the radio is down no matter what we asked for.
 *
 *  The credentials are deliberately kept: they are what we WANT the AP to be,
 *  they did not change because the chip restarted, and a caller re-arming after
 *  a reboot wants to ask for the same SSID rather than invent a second one.
 *  Only `up` is a claim about the 8711, so only `up` is retracted. */
static void on_link_ready(void *user)
{
    (void)user;

    k_mutex_lock(&s_lock, K_FOREVER);
    bool was_up = s_ap.up || s_ap.starting;
    s_ap.up       = false;
    s_ap.starting = false;
    k_mutex_unlock(&s_lock);

    if (was_up)
    {
        EBADGE_WARN("at_wifi: 8711 rebooted -- the AP is down, credentials kept");
    }
}

int at_wifi_init(void)
{
    if (s_inited)
    {
        return 0;
    }

    k_mutex_init(&s_lock);
    memset(&s_ap, 0, sizeof(s_ap));

    int rc = at_client_init();

    if (rc != 0)
    {
        return rc;
    }

    at_client_add_ready_cb(on_link_ready, NULL);

    s_inited = true;
    EBADGE_LOG("at_wifi: up (we own the AP credentials; no state feed, no "
               "association events)");
    return 0;
}

#endif /* CONFIG_SPI_AT_MASTER */
