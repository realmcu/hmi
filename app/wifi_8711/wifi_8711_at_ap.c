/**
 * @file    wifi_8711_at_ap.c
 * @brief   SoftAP semantics -- parse WLSTATE/WLSTARTAP replies, cache the result.
 *
 * The parser is line-oriented and deliberately tolerant.  It anchors each field
 * at the start of a line rather than searching the whole buffer, because
 * "PASSWORD=" contains no substring trap but "IP=" does: a WLSTATE reply's
 * per-client lines are "CLIENT=1 MAC=.. IP=.. RSSI=..", so a naive strstr("IP=")
 * would find the CLIENT's address before the AP's and report the phone's IP as
 * the gateway.  Line anchoring is what keeps that from happening.
 *
 * Unknown lines are skipped rather than treated as errors.  The 8711's reply
 * format is the vendor's to extend, and a firmware that adds a field should not
 * make this parser fail -- it should make it ignore one line.
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
 *  Cache
 *
 *  Written on the AT layer's callback context, read by BLE command handlers on
 *  l2_task.  Both are thread context, and the struct is small but not
 *  atomically copyable, so a mutex is required -- a handler reading it mid-write
 *  could otherwise pair a new SSID with an old password.
 *----------------------------------------------------------------------------*/
static K_MUTEX_DEFINE(s_cache_lock);
static wifi_8711_ap_info_t s_cache;
static bool                s_cache_valid;
static uint32_t            s_cache_ms;

/*----------------------------------------------------------------------------*
 *  Pending request -- one at a time, mirroring the AT layer's single flight
 *----------------------------------------------------------------------------*/
static wifi_8711_ap_cb_t s_cb;
static void             *s_cb_user;

/*----------------------------------------------------------------------------*
 *  Parsing helpers
 *----------------------------------------------------------------------------*/

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

bool wifi_8711_at_ap_parse(const char *text, wifi_8711_ap_info_t *out)
{
    if (text == NULL || out == NULL)
    {
        return false;
    }

    memset(out, 0, sizeof(*out));

    /* sec.9.3: an unrecognised command, or WLSTATE while the AP is down, comes
     * back as "[AT]:ERROR".  Parsing that as a state reply would produce an
     * all-zero struct that a caller could mistake for "AP up, no clients". */
    if (strstr(text, "[AT]:ERROR") != NULL)
    {
        return false;
    }

    bool have_ssid = false;

    for (const char *line = text; *line != '\0'; line = next_line(line))
    {
        const char *val = NULL;

        if (line_is(line, "SSID=", &val))
        {
            copy_line_value(out->ssid, sizeof(out->ssid), val);
            have_ssid = (out->ssid[0] != '\0');
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
            out->port = (uint16_t)strtoul(val, NULL, 10);
        }
        else if (line_is(line, "CLIENTS=", &val))
        {
            out->clients = (uint8_t)strtoul(val, NULL, 10);
        }
        /* CLIENT=n MAC=.. / [+WLSTATE]:OK / [+WLSTARTAP]:OK and anything the
         * vendor adds later fall through deliberately -- see the file header. */
    }

    /* An SSID is the minimum that makes a reply useful: it is the one field
     * both command shapes always carry, and without it there is nothing to tell
     * the phone.  WLSTARTAP replies stop here, with no IP/port/clients. */
    out->running = have_ssid;
    return have_ssid;
}

/*----------------------------------------------------------------------------*
 *  Cache update + completion
 *----------------------------------------------------------------------------*/
static void ap_complete(bool ok, const wifi_8711_ap_info_t *info)
{
    wifi_8711_ap_cb_t   cb   = s_cb;
    void               *user = s_cb_user;
    wifi_8711_ap_info_t snapshot;

    s_cb      = NULL;
    s_cb_user = NULL;

    if (ok && info != NULL)
    {
        k_mutex_lock(&s_cache_lock, K_FOREVER);
        /* Merge rather than overwrite: a WLSTARTAP reply carries only SSID and
         * password, so a blind assignment would wipe the IP and port a previous
         * WLSTATE had established and leave the phone with no endpoint. */
        s_cache.running = info->running;
        if (info->ssid[0] != '\0')
        {
            memcpy(s_cache.ssid, info->ssid, sizeof(s_cache.ssid));
            memcpy(s_cache.password, info->password, sizeof(s_cache.password));
        }
        if (info->ip != 0U)   { s_cache.ip      = info->ip; }
        if (info->port != 0U) { s_cache.port    = info->port; }
        s_cache.clients = info->clients;
        s_cache_valid   = true;
        s_cache_ms      = k_uptime_get_32();
        snapshot        = s_cache;
        k_mutex_unlock(&s_cache_lock);
    }
    else
    {
        memset(&snapshot, 0, sizeof(snapshot));
    }

    if (cb != NULL)
    {
        cb(ok, &snapshot, user);
    }
}

/** AT-layer completion: parse, cache, hand up.  Runs on the transport thread
 *  or the workqueue, never l2_task. */
static void on_at_reply(wifi_8711_at_result_t res, const char *text, size_t len,
                        void *user)
{
    ARG_UNUSED(user);
    ARG_UNUSED(len);

    /* TRUNC is accepted deliberately: the truncation eats the tail of the reply,
     * which is the CLIENT list, while SSID/PASSWORD/IP/PORT come first.  The AT
     * layer has already warned about it.  Anything else has no text to parse. */
    if (res != WIFI_8711_AT_OK && res != WIFI_8711_AT_ERR_TRUNC)
    {
        EBADGE_WARN1("wifi8711 ap: query failed (%s)",
                     wifi_8711_at_result_str(res));
        ap_complete(false, NULL);
        return;
    }

    wifi_8711_ap_info_t info;
    if (!wifi_8711_at_ap_parse(text, &info))
    {
        /* Reached when the AP is genuinely down: WLSTATE requires a running AP
         * and answers [AT]:ERROR otherwise (sec.9 note).  Log the head of the
         * reply -- an unexpected format is the other reason to land here and
         * the two need different fixes. */
        EBADGE_WARN("wifi8711 ap: reply not parseable as AP state (AP down?)");
        EBADGE_LOG1("wifi8711 ap: reply head: %.64s", text);
        ap_complete(false, NULL);
        return;
    }

    EBADGE_LOG2("wifi8711 ap: ssid=\"%s\" clients=%u", info.ssid,
                (unsigned)info.clients);
    EBADGE_LOG2("wifi8711 ap: ip=%08x port=%u", (unsigned)info.ip,
                (unsigned)info.port);
    ap_complete(true, &info);
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
static int ap_submit(const char *cmd, wifi_8711_ap_cb_t cb, void *user)
{
    int rc;

    /* Record the caller BEFORE submitting: on a fast link the AT layer could in
     * principle complete before submit() returns, and a callback that found
     * s_cb still empty would drop the answer. */
    s_cb      = cb;
    s_cb_user = user;

    rc = wifi_8711_at_submit(cmd, on_at_reply, NULL);
    if (rc != 0)
    {
        s_cb      = NULL;
        s_cb_user = NULL;
    }
    return rc;
}

int wifi_8711_at_ap_query(wifi_8711_ap_cb_t cb, void *user)
{
    return ap_submit(SPI_AT_CMD_WLSTATE, cb, user);
}

int wifi_8711_at_ap_start(wifi_8711_ap_cb_t cb, void *user)
{
    return ap_submit(SPI_AT_CMD_WLSTARTAP, cb, user);
}

bool wifi_8711_at_ap_cached(wifi_8711_ap_info_t *out)
{
    bool valid;

    k_mutex_lock(&s_cache_lock, K_FOREVER);
    valid = s_cache_valid;
    if (valid && out != NULL)
    {
        *out = s_cache;
    }
    k_mutex_unlock(&s_cache_lock);
    return valid;
}

uint32_t wifi_8711_at_ap_cache_age_ms(void)
{
    uint32_t age;

    k_mutex_lock(&s_cache_lock, K_FOREVER);
    age = s_cache_valid ? (k_uptime_get_32() - s_cache_ms) : UINT32_MAX;
    k_mutex_unlock(&s_cache_lock);
    return age;
}

#endif /* CONFIG_WIFI_8711 */
