/**
 * @file    wifi_8711_at_ap.c
 * @brief   The one global Wi-Fi state: one parser, three feeds, one copy.
 *
 * ---------------------------------------------------------------------------
 * THREE CARRIERS, ONE BLOCK, ONE UPDATE PATH
 * ---------------------------------------------------------------------------
 * sec.7.4 defines one state block and three ways it reaches us -- the ~1 Hz POLL
 * payload, an AT+WLSTATE reply, and the unsolicited notification pushed when a
 * requested start succeeds.  They differ only in the terminator line appended.
 *
 * So every one of them lands in state_merge(), and that function is the ONLY
 * writer of the credentials -- wifi_8711_at_ap_ingest() is the public door onto
 * it, used by the AT layer for the POLL feed and the unsolicited notification,
 * and by on_at_reply() for a WLSTATE answer.  POLL is the primary feed; the
 * query is a way to ask for the same block a beat sooner.  One writer is what
 * makes "the global state" a fact rather than an aspiration: there is no second,
 * narrower path that could fill half the struct and disagree with the first.
 *
 * The two verdict commands stay off that path, as they always have:
 *
 *   the state block  a DESCRIPTION -- parsed, merged into the global state,
 *                    timestamped.
 *   WLSTARTAP        a VERDICT -- "[+WLSTARTAP]:OK" means the REQUEST was
 *                    accepted, and STATE=UP/STARTING says which of the two
 *                    happened.  Carries no credentials at all since sec.12.8.
 *   WLSTOPAP         a verdict too, the other way round.
 *
 * wifi_8711_at_ap_parse() rejects a WLSTARTAP body outright rather than merely
 * being pointed elsewhere, so the rule holds even if a future caller reaches for
 * the wrong function.
 *
 * ---------------------------------------------------------------------------
 * WHY THE PARSER IS LINE-ANCHORED
 * ---------------------------------------------------------------------------
 * Each field is anchored at the start of a line rather than searched for in the
 * whole buffer, because "PASSWORD=" contains no substring trap but "IP=" does: a
 * state block's per-client lines are "CLIENT=1 MAC=.. IP=.. RSSI=..", so a naive
 * strstr("IP=") would find the CLIENT's address before the AP's and report the
 * phone's IP as the gateway.  Line anchoring is what keeps that from happening.
 *
 * Unknown lines are skipped rather than treated as errors.  The 8711's reply
 * format is the vendor's to extend, and a firmware that adds a field should not
 * make this parser fail -- it should make it ignore one line.
 *
 * ---------------------------------------------------------------------------
 * WHY IT MERGES INSTEAD OF OVERWRITING
 * ---------------------------------------------------------------------------
 * `state` and `clients` are taken verbatim from every block -- they are what the
 * block is FOR.  The credentials are merged: a zero-valued field means "no news"
 * and leaves the previous value in place.
 *
 * That is not a hedge against a lying peer, it is two concrete cases.  A
 * truncated reply loses its tail, and the tail can reach FILE_PORT.  And an
 * AP=DOWN block reports SSID= with nothing after it (sec.7.4 expresses "no
 * value" as a zero value), so overwriting would erase the credentials every time
 * the radio cycles -- while the 8711 has no way to change them, so they were
 * still correct.  Callers are told to read `state` before the credentials, which
 * is what makes keeping them safe.
 */
#include <zephyr/kernel.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_8711_at.h"
#include "wifi_8711_at_ap.h"
#include "spi_at_protocol.h"
#include "../protocol/ebadge_log.h"

#if defined(CONFIG_WIFI_8711)

/*----------------------------------------------------------------------------*
 *  The global state
 *
 *  Written from the transport thread (the POLL feed and the AT callbacks), read
 *  by BLE command handlers on l2_task.  Both are thread context, and the struct
 *  is small but not atomically copyable, so a mutex is required -- a handler
 *  reading it mid-write could otherwise pair a new SSID with an old password.
 *
 *  s_state_ms is stamped by state_merge() and nothing else.  It measures "how
 *  long since the 8711 last described its Wi-Fi", which on a healthy link is
 *  under one beat; the verdict replies deliberately do not touch it, because
 *  they describe nothing and a fresh stamp off one of them would hide a stopped
 *  beat.
 *----------------------------------------------------------------------------*/
static K_MUTEX_DEFINE(s_state_lock);
static wifi_8711_ap_info_t s_state;
static bool                s_state_valid;
static uint32_t            s_state_ms;

/*----------------------------------------------------------------------------*
 *  Pending request -- one at a time, mirroring the AT layer's single flight
 *----------------------------------------------------------------------------*/
static wifi_8711_ap_cb_t s_cb;
static void             *s_cb_user;

/*----------------------------------------------------------------------------*
 *  Parsing helpers
 *----------------------------------------------------------------------------*/

const char *wifi_8711_ap_state_str(wifi_8711_ap_state_t state)
{
    switch (state)
    {
    case WIFI_8711_AP_UP:       return SPI_AT_AP_UP;
    case WIFI_8711_AP_STARTING: return SPI_AT_AP_STARTING;
    case WIFI_8711_AP_DOWN:     return SPI_AT_AP_DOWN;
    default:                    return "?";
    }
}

/** Advance past the current line, returning the start of the next one (or the
 *  terminating NUL).  Handles CR, LF and CRLF, because the 8711 emits CRLF but
 *  its own docs show bare LF in places. */
static const char *next_line(const char *p)
{
    while (*p != '\0' && *p != '\r' && *p != '\n')
    {
        p++;
    }
    while (*p == '\r' || *p == '\n')
    {
        p++;
    }
    return p;
}

/** True if @p line starts with @p key.  Used to anchor at line start, which is
 *  what stops a CLIENT line's "IP=" from being mistaken for the AP's. */
static bool line_is(const char *line, const char *key, const char **out_val)
{
    size_t n = strlen(key);

    if (strncmp(line, key, n) != 0)
    {
        return false;
    }
    *out_val = line + n;
    return true;
}

/** Copy the rest of the line into @p dst, NUL-terminated and trimmed of
 *  trailing whitespace.  Truncates rather than overflowing. */
static void copy_line_value(char *dst, size_t dst_size, const char *val)
{
    size_t n = 0;

    while (val[n] != '\0' && val[n] != '\r' && val[n] != '\n' &&
           n < (dst_size - 1U))
    {
        n++;
    }
    memcpy(dst, val, n);
    /* Trim trailing spaces: a value written as "SSID=foo " would otherwise
     * carry the space into the SSID the phone is told to look for, and the
     * network would appear not to exist. */
    while (n > 0U && (dst[n - 1U] == ' ' || dst[n - 1U] == '\t'))
    {
        n--;
    }
    dst[n] = '\0';
}

/** Parse a dotted-quad into host order.  Returns 0 on anything malformed,
 *  which the caller treats as "IP absent" -- a partially parsed address would
 *  be worse than none, because it would look valid to the phone. */
static uint32_t parse_ipv4(const char *val)
{
    uint32_t out = 0U;

    for (int i = 0; i < 4; i++)
    {
        /* strtoul, not atoi: it reports where it stopped, which is how the
         * separator gets validated. */
        char         *end = NULL;
        unsigned long o   = strtoul(val, &end, 10);

        if (end == val || o > 255UL)
        {
            return 0U;
        }
        out = (out << 8) | (uint32_t)o;
        val = end;
        if (i < 3)
        {
            if (*val != '.')
            {
                return 0U;
            }
            val++;
        }
    }
    return out;
}

/** Map the value of an AP= line onto the tri-state.  Returns false if it is
 *  none of the three, which means the line was not the one sec.7.4 promises and
 *  the whole block is suspect. */
static bool parse_ap_state(const char *val, wifi_8711_ap_state_t *out)
{
    /* Longest first: "UP" is a prefix of nothing here, but checking DOWN and
     * STARTING before it keeps the intent obvious if the vendor ever adds a
     * value that does share a prefix. */
    if (strncmp(val, SPI_AT_AP_STARTING, strlen(SPI_AT_AP_STARTING)) == 0)
    {
        *out = WIFI_8711_AP_STARTING;
        return true;
    }
    if (strncmp(val, SPI_AT_AP_DOWN, strlen(SPI_AT_AP_DOWN)) == 0)
    {
        *out = WIFI_8711_AP_DOWN;
        return true;
    }
    if (strncmp(val, SPI_AT_AP_UP, strlen(SPI_AT_AP_UP)) == 0)
    {
        *out = WIFI_8711_AP_UP;
        return true;
    }
    return false;
}

bool wifi_8711_at_ap_parse(const char *text, wifi_8711_ap_info_t *out)
{
    if (text == NULL || out == NULL)
    {
        return false;
    }

    memset(out, 0, sizeof(*out));

    /* sec.9.3: an unrecognised command comes back as a bare "[AT]:ERROR" with no
     * block at all.  Note this is NOT the same as "[+WLSTATE]:ERROR", which
     * sec.10.1 appends to a perfectly good block to say the AP is unavailable --
     * that one parses normally and reports AP=DOWN, which is the answer. */
    if (strstr(text, "[AT]:ERROR") != NULL)
    {
        return false;
    }

    /* A WLSTARTAP body is refused here rather than parsed.  Since sec.12.8 it
     * carries no credentials at all, so there is nothing in it for this function;
     * what it does carry is a STATE= line, and reading that as if it were an AP=
     * line would let a "request accepted" answer masquerade as a description of
     * the radio.  Its answer is a verdict and belongs to on_start_reply(). */
    if (strstr(text, SPI_AT_RSP_WLSTARTAP_OK) != NULL)
    {
        return false;
    }

    bool have_state = false;

    for (const char *line = text; *line != '\0'; line = next_line(line))
    {
        const char *val = NULL;

        if (line_is(line, SPI_AT_KEY_AP, &val))
        {
            /* The first line of every block and the verdict for the whole parse.
             * Note there is no branch on it: sec.7.4 fixes the layout, so a down
             * AP still emits every key below, with zero values, and skipping them
             * would leave the struct describing whatever was in it before. */
            have_state = parse_ap_state(val, &out->state);
        }
        else if (line_is(line, "SSID=", &val))
        {
            copy_line_value(out->ssid, sizeof(out->ssid), val);
        }
        else if (line_is(line, "PASSWORD=", &val))
        {
            copy_line_value(out->password, sizeof(out->password), val);
        }
        else if (line_is(line, "IP=", &val))
        {
            /* Anchored at line start, so this is the AP's gateway address and
             * not one of the CLIENT lines' addresses. */
            out->ip = parse_ipv4(val);
        }
        else if (line_is(line, "PORT=", &val))
        {
            /* The preview port.  Anchored at the line start, so "FILE_PORT="
             * cannot land here -- strstr("PORT=") would have matched it and
             * silently reported the upload port as the preview one. */
            out->stream_port = (uint16_t)strtoul(val, NULL, 10);
        }
        else if (line_is(line, "FILE_PORT=", &val))
        {
            /* The EBXF upload port (SPI spec sec.6).  This used to be skipped
             * with a comment saying nothing connected to it yet, which stopped
             * being true once file transfers were wired up -- and the consequence
             * was that AP_INFO advertised the preview port for a file transfer,
             * so the phone connected to a door that only accepts bare JPEG and
             * the transfer died with no diagnostic. */
            out->file_port = (uint16_t)strtoul(val, NULL, 10);
        }
        else if (line_is(line, "CHANNEL=", &val))
        {
            /* Range-checked before the narrowing cast: an out-of-range value
             * would otherwise alias onto a plausible channel (300 -> 44) and be
             * handed to the phone as fact.  0 already means "unknown". */
            unsigned long ch = strtoul(val, NULL, 10);

            out->channel = (ch >= 1UL && ch <= 196UL) ? (uint8_t)ch : 0U;
        }
        else if (line_is(line, "CLIENTS=", &val))
        {
            /* The authoritative count.  The CLIENT= lines below are not counted:
             * sec.7.4 says their number is variable and CLIENTS= is the only
             * count to trust. */
            out->clients = (uint8_t)strtoul(val, NULL, 10);
        }
        /* CLIENT=n MAC=.. / the "[+WLSTATE]:OK"|":ERROR" terminator / anything
         * the vendor adds later fall through deliberately -- see the header. */
    }

    /* The AP= line is the verdict, and deliberately NOT the presence of an SSID.
     * Requiring one made an AP=DOWN block -- which is informative, and the state
     * the 8711 is in every time before we ask it to start -- indistinguishable
     * from a broken link, and the caller's reaction to those two differs. */
    return have_state;
}

/*----------------------------------------------------------------------------*
 *  The single writer
 *----------------------------------------------------------------------------*/

/** Merge a parsed block into the global state and stamp it.  @p info must have
 *  come from a successful wifi_8711_at_ap_parse().
 *
 *  Takes the lock and runs nothing, so it is callable from the transport
 *  thread's slot sink. */
static void state_merge(const wifi_8711_ap_info_t *info)
{
    k_mutex_lock(&s_state_lock, K_FOREVER);

    /* Verbatim: these two are what the block is for, and both have a meaningful
     * zero.  "AP=DOWN" and "CLIENTS=0" are news, not the absence of it. */
    s_state.state   = info->state;
    s_state.clients = info->clients;

    /* Merged: see the header.  A zero here is either a truncated tail or the
     * empty value an AP=DOWN block reports, and in neither case has the 8711
     * told us the credentials changed -- it has no way to change them. */
    if (info->ssid[0] != '\0')
    {
        memcpy(s_state.ssid, info->ssid, sizeof(s_state.ssid));
        memcpy(s_state.password, info->password, sizeof(s_state.password));
    }
    if (info->ip          != 0U) { s_state.ip          = info->ip; }
    if (info->stream_port != 0U) { s_state.stream_port = info->stream_port; }
    if (info->file_port   != 0U) { s_state.file_port   = info->file_port; }
    if (info->channel     != 0U) { s_state.channel     = info->channel; }

    s_state_valid = true;
    s_state_ms    = k_uptime_get_32();

    k_mutex_unlock(&s_state_lock);
}

bool wifi_8711_at_ap_ingest(const char *text)
{
    wifi_8711_ap_info_t info;

    if (!wifi_8711_at_ap_parse(text, &info))
    {
        return false;
    }
    state_merge(&info);
    return true;
}

/*----------------------------------------------------------------------------*
 *  Query completion
 *----------------------------------------------------------------------------*/
static void ap_complete(bool ok)
{
    wifi_8711_ap_cb_t   cb   = s_cb;
    void               *user = s_cb_user;
    wifi_8711_ap_info_t snapshot;

    s_cb      = NULL;
    s_cb_user = NULL;

    /* The snapshot comes from the global state, not from the reply that just
     * arrived, and that is the point of this layer: state_merge() has already
     * folded the reply in, so reading it back hands the caller the merged truth
     * (credentials that survived a truncated tail included) rather than the
     * fragment that happened to be on the wire. */
    if (!ok || !wifi_8711_at_ap_cached(&snapshot))
    {
        memset(&snapshot, 0, sizeof(snapshot));
    }

    if (cb != NULL)
    {
        cb(ok, &snapshot, user);
    }
}

/** AT-layer completion for WLSTATE.  Runs on the transport thread or the
 *  workqueue, never l2_task. */
static void on_at_reply(wifi_8711_at_result_t res, const char *text, size_t len,
                        void *user)
{
    ARG_UNUSED(user);
    ARG_UNUSED(len);

    /* TRUNC is accepted deliberately: the truncation eats the tail of the block,
     * which is the CLIENT list, while AP=/SSID/PASSWORD/IP/PORT come first.  The
     * AT layer has already warned about it.  Anything else has no text to
     * parse. */
    if (res != WIFI_8711_AT_OK && res != WIFI_8711_AT_ERR_TRUNC)
    {
        EBADGE_WARN1("wifi8711 ap: query failed (%s)",
                     wifi_8711_at_result_str(res));
        ap_complete(false);
        return;
    }

    if (!wifi_8711_at_ap_ingest(text))
    {
        /* Not "the AP is down" -- since sec.10.1 that answer arrives as a full
         * block with "[+WLSTATE]:ERROR" appended and ingests fine.  Reaching here
         * means the body was a bare [AT]:ERROR or a shape we do not know, and the
         * AT layer has already dumped it verbatim. */
        EBADGE_WARN("wifi8711 ap: reply is not a state block (bare [AT]:ERROR, or"
                    " a format we do not know -- see the dump above)");
        ap_complete(false);
        return;
    }

    /* The raw body is NOT printed here.  The AT layer dumps every RESPONSE
     * verbatim before it even matches the sequence (see at_slot_sink), which
     * covers replies this function never sees -- so repeating it would print the
     * same body twice and still not cover the dropped ones.  What is left here is
     * the INTERPRETATION, and the two disagreeing is the bug worth catching: a
     * field the vendor renamed shows up as "absent" below while being plainly
     * present in the dump above. */
    wifi_8711_ap_info_t now;
    if (wifi_8711_at_ap_cached(&now))
    {
        EBADGE_LOG3("[8711->8773] ap parsed: state=%s ssid=\"%s\" clients=%u",
                    wifi_8711_ap_state_str(now.state), now.ssid,
                    (unsigned)now.clients);
        EBADGE_LOG4("[8711->8773] ap parsed: ip=%08x stream_port=%u"
                    " file_port=%u channel=%u",
                    (unsigned)now.ip, (unsigned)now.stream_port,
                    (unsigned)now.file_port, (unsigned)now.channel);
    }
    ap_complete(true);
}

/*----------------------------------------------------------------------------*
 *  Start
 *
 *  Off the state-block path because the reply is a verdict, not a description:
 *  since sec.12.8 it carries no credentials at all, only "[+WLSTARTAP]:OK" and a
 *  STATE= line saying whether the AP was already up or is now coming up.  So the
 *  only field of the global state it can honestly touch is `state`.
 *----------------------------------------------------------------------------*/
static void start_complete(bool ok, wifi_8711_ap_state_t state)
{
    wifi_8711_ap_cb_t   cb   = s_cb;
    void               *user = s_cb_user;
    wifi_8711_ap_info_t snapshot;

    s_cb      = NULL;
    s_cb_user = NULL;

    /* All-zero, on success as well as failure, apart from the state below: this
     * reply described nothing.  Handing back the global state instead would pass
     * on its `clients` count, and the caller reads exactly that field to decide
     * the phone has associated -- so a non-zero carried in from before the start
     * would open a transfer against a phone that is not on the network. */
    memset(&snapshot, 0, sizeof(snapshot));

    if (ok)
    {
        k_mutex_lock(&s_state_lock, K_FOREVER);
        /* The only field touched, and s_state_ms is NOT re-stamped: the staleness
         * clock measures how long since the 8711 last DESCRIBED its Wi-Fi, and
         * this reply did not.  Re-stamping it off a verdict would hide a stopped
         * POLL beat for as long as someone kept asking for starts.
         *
         * s_state_valid is left as it was for the same reason -- it means "a block
         * has arrived at least once", which a start neither makes nor breaks. */
        s_state.state = state;
        k_mutex_unlock(&s_state_lock);
        snapshot.state = state;
    }

    if (cb != NULL)
    {
        /* `ok` means the request was accepted; snapshot.state says whether that
         * means "already up" or "coming up".  Callers that need the credentials
         * read the global state, which the next beat fills. */
        cb(ok, &snapshot, user);
    }
}

static void on_start_reply(wifi_8711_at_result_t res, const char *text,
                           size_t len, void *user)
{
    ARG_UNUSED(user);
    ARG_UNUSED(len);

    if (res != WIFI_8711_AT_OK && res != WIFI_8711_AT_ERR_TRUNC)
    {
        /* The AT layer has already logged the specific reason. */
        EBADGE_WARN1("wifi8711 ap: start transaction failed (%s)",
                     wifi_8711_at_result_str(res));
        start_complete(false, WIFI_8711_AP_DOWN);
        return;
    }

    /* TRUNC is accepted for the same reason as on the state-block path: the OK
     * line comes FIRST in this reply (sec.10.2), so a truncation can only cost us
     * the STATE= line -- and that degrades to STARTING below, which is the
     * conservative reading. */
    if (text != NULL && strstr(text, SPI_AT_RSP_WLSTARTAP_OK) != NULL)
    {
        /* OK means the REQUEST was accepted, not that the AP is up.  STATE= is
         * what distinguishes them, and defaulting the absent case to STARTING
         * rather than UP is deliberate: STARTING says "wait for the beat", which
         * is harmless if the AP was in fact already up, whereas UP would have a
         * caller advertise credentials that do not exist yet. */
        wifi_8711_ap_state_t state =
            (strstr(text, SPI_AT_RSP_STARTAP_STATE_UP) != NULL)
            ? WIFI_8711_AP_UP : WIFI_8711_AP_STARTING;

        EBADGE_LOG1("[8711->8773] ap start accepted, state=%s (credentials come"
                    " from the state block)", wifi_8711_ap_state_str(state));
        start_complete(true, state);
        return;
    }

    /* WLSTARTAP has no command-specific error line, so a refusal arrives as the
     * shared "[AT]:ERROR" -- which is also what an 8711 that does not know the
     * command returns.  Not worth distinguishing: the caller retries either way,
     * and the verbatim dump above says which it was. */
    EBADGE_WARN("wifi8711 ap: start not confirmed (no [+WLSTARTAP]:OK -- see the"
                " dump above)");
    start_complete(false, WIFI_8711_AP_DOWN);
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
static int ap_submit(const char *cmd, wifi_8711_at_cb_t on_reply,
                     wifi_8711_ap_cb_t cb, void *user)
{
    int rc;

    /* Record the caller BEFORE submitting: on a fast link the AT layer could in
     * principle complete before submit() returns, and a callback that found
     * s_cb still empty would drop the answer. */
    s_cb      = cb;
    s_cb_user = user;

    rc = wifi_8711_at_submit(cmd, on_reply, NULL);
    if (rc != 0)
    {
        s_cb      = NULL;
        s_cb_user = NULL;
    }
    return rc;
}

int wifi_8711_at_ap_query(wifi_8711_ap_cb_t cb, void *user)
{
    return ap_submit(SPI_AT_CMD_WLSTATE, on_at_reply, cb, user);
}

int wifi_8711_at_ap_start(wifi_8711_ap_cb_t cb, void *user)
{
    /* Shares s_cb with the query -- the AT layer is single flight, so only one of
     * the two can be outstanding -- but not the reply handler: the two replies
     * answer different questions.  See the file header. */
    return ap_submit(SPI_AT_CMD_WLSTARTAP, on_start_reply, cb, user);
}

/*----------------------------------------------------------------------------*
 *  Stop
 *
 *  Kept off ap_submit()/on_at_reply() entirely rather than sharing them, because
 *  the reply has no fields at all: "[+WLSTOPAP]:OK" is not a state block, so
 *  wifi_8711_at_ap_parse() would refuse it and a perfectly good stop would be
 *  reported as a failure.  A verdict and a description get different code.
 *----------------------------------------------------------------------------*/
static wifi_8711_ap_stop_cb_t s_stop_cb;
static void                  *s_stop_user;

static void stop_complete(bool ok)
{
    wifi_8711_ap_stop_cb_t cb   = s_stop_cb;
    void                  *user = s_stop_user;

    s_stop_cb   = NULL;
    s_stop_user = NULL;

    if (ok)
    {
        /* Only the state and the client count are touched.  The credentials
         * outlive an AP cycle -- the 8711 cannot change them -- so clearing them
         * would cost the next transfer nothing but would make the next AP_INFO
         * before the following beat answer with an empty SSID.
         *
         * s_state_valid is deliberately NOT cleared: it means "the link has
         * described itself at least once", which stays true.  Nor is s_state_ms
         * re-stamped -- see start_complete(). */
        k_mutex_lock(&s_state_lock, K_FOREVER);
        s_state.state = WIFI_8711_AP_DOWN;
        /* An AP that is down has no associated stations, and this is the field a
         * caller reads to decide the phone is present. */
        s_state.clients = 0U;
        k_mutex_unlock(&s_state_lock);
    }

    if (cb != NULL)
    {
        cb(ok, user);
    }
}

static void on_stop_reply(wifi_8711_at_result_t res, const char *text, size_t len,
                          void *user)
{
    ARG_UNUSED(user);
    ARG_UNUSED(len);

    if (res != WIFI_8711_AT_OK)
    {
        /* The AT layer has already logged the specific reason. */
        EBADGE_WARN1("wifi8711 ap: stop transaction failed (%s)",
                     wifi_8711_at_result_str(res));
        stop_complete(false);
        return;
    }

    if (text != NULL && strstr(text, SPI_AT_RSP_WLSTOPAP_OK) != NULL)
    {
        EBADGE_LOG("[8711->8773] ap stopped");
        stop_complete(true);
        return;
    }

    /* Both failure shapes land here: the command-specific "[+WLSTOPAP]:ERROR"
     * and the generic "[AT]:ERROR" an 8711 build predating this command returns.
     * Neither is worth distinguishing in code -- the caller's response is the
     * same -- but the log says which, because "the AP was already down" and "this
     * firmware does not have the command" need different fixes. */
    EBADGE_WARN1("wifi8711 ap: stop refused (%s) -- AP already down, a start in "
                 "progress, or a firmware without WLSTOPAP",
                 (text != NULL &&
                  strstr(text, SPI_AT_RSP_WLSTOPAP_ERROR) != NULL)
                 ? "[+WLSTOPAP]:ERROR" : "no OK line");
    stop_complete(false);
}

int wifi_8711_at_ap_stop(wifi_8711_ap_stop_cb_t cb, void *user)
{
    int rc;

    /* Recorded before the submit, same reason as ap_submit(): on a fast link the
     * completion could in principle run before submit() returns. */
    s_stop_cb   = cb;
    s_stop_user = user;

    rc = wifi_8711_at_submit(SPI_AT_CMD_WLSTOPAP, on_stop_reply, NULL);
    if (rc != 0)
    {
        s_stop_cb   = NULL;
        s_stop_user = NULL;
    }
    return rc;
}

bool wifi_8711_at_ap_cached(wifi_8711_ap_info_t *out)
{
    bool valid;

    k_mutex_lock(&s_state_lock, K_FOREVER);
    valid = s_state_valid;
    if (valid && out != NULL)
    {
        *out = s_state;
    }
    k_mutex_unlock(&s_state_lock);
    return valid;
}

uint32_t wifi_8711_at_ap_cache_age_ms(void)
{
    uint32_t age;

    k_mutex_lock(&s_state_lock, K_FOREVER);
    age = s_state_valid ? (k_uptime_get_32() - s_state_ms) : UINT32_MAX;
    k_mutex_unlock(&s_state_lock);
    return age;
}

#endif /* CONFIG_WIFI_8711 */
