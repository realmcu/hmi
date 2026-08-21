/**
 * @file    wifi_8711_at_data.c
 * @brief   Reserved data tunnel -- framing, hex codec, unsupported-latch.
 *
 * See the header for why these two commands exist and why they currently fail.
 *
 * The reply parser is line-oriented and anchored at the start of a line, the
 * same discipline as wifi_8711_at_ap.c and for the same reason: "DATA=" is short
 * enough to occur inside a hex payload by chance, and a strstr() that found the
 * second one would decode from the wrong offset and report plausible garbage.
 * Anchoring makes that impossible rather than unlikely.
 */
#include <zephyr/kernel.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include "wifi_8711.h"
#include "wifi_8711_at.h"
#include "wifi_8711_at_data.h"
#include "spi_at_protocol.h"
#include "../protocol/ebadge_log.h"

#if defined(CONFIG_WIFI_8711)

/*----------------------------------------------------------------------------*
 *  Size budget -- proved, not asserted in a comment
 *
 *  The header derives SEND_MAX from the peer's 128 B parse buffer by hand.
 *  These checks make the compiler agree, so editing the prefix or the cap
 *  without redoing the arithmetic is a build failure rather than a silently
 *  dropped command on the wire (the 8711 drops an over-long COMMAND without
 *  saying anything -- the worst possible failure mode to debug).
 *----------------------------------------------------------------------------*/
#define DATA_SEND_CMD_LEN  (sizeof(WIFI_8711_AT_CMD_WLSEND_PREFIX) - 1U + \
                            (WIFI_8711_AT_DATA_SEND_MAX * 2U) + 2U + 1U)

BUILD_ASSERT(DATA_SEND_CMD_LEN <= WIFI_8711_AT_COMMAND_MAX,
             "AT+WLSEND framing exceeds the 8711's 128 B parse buffer -- "
             "lower WIFI_8711_AT_DATA_SEND_MAX");
BUILD_ASSERT((WIFI_8711_AT_DATA_RECV_MAX * 2U) + 64U <= WIFI_8711_AT_TEXT_MAX,
             "AT+WLRECV reply would not fit the AT text buffer -- "
             "lower WIFI_8711_AT_DATA_RECV_MAX or raise WIFI_8711_AT_TEXT_MAX");

/*----------------------------------------------------------------------------*
 *  State
 *
 *  Single flight, mirroring the AT layer below: there is one TX slot, so there
 *  is no point in this layer holding a queue the layer beneath cannot serve.
 *----------------------------------------------------------------------------*/
static wifi_8711_data_rx_cb_t   s_rx_cb;
static void                    *s_rx_user;
static wifi_8711_data_done_cb_t s_done_cb;
static void                    *s_done_user;
static wifi_8711_data_stats_t   s_stats;

/* Latched false on the first "[AT]:ERROR".  Starts true because "not known to
 * be unsupported" is the honest state at boot -- see the header. */
static bool s_supported = true;

/* Staging buffer for the framed send command.  Static rather than on the
 * caller's stack: this is up to 128 B and the AT layer copies it synchronously,
 * but the callers are BLE handlers on l2_task whose stack is shared with the
 * whole protocol dispatch.  Safe to be static precisely BECAUSE the layer is
 * single flight -- a second sender is refused with -EBUSY before it gets
 * here. */
static char s_send_cmd[DATA_SEND_CMD_LEN];

/*----------------------------------------------------------------------------*
 *  Hex codec
 *----------------------------------------------------------------------------*/
static const char HEX_DIGITS[] = "0123456789ABCDEF";

/** Encode @p len bytes as uppercase hex into @p dst, NUL-terminating it.
 *  @p dst must hold 2*len + 1 bytes; the caller has already bounded len. */
static void hex_encode(const uint8_t *src, size_t len, char *dst)
{
    for (size_t i = 0U; i < len; i++)
    {
        dst[i * 2U]      = HEX_DIGITS[(src[i] >> 4) & 0x0FU];
        dst[i * 2U + 1U] = HEX_DIGITS[src[i] & 0x0FU];
    }
    dst[len * 2U] = '\0';
}

/** One hex digit to its value, or -1 if it is not a hex digit. */
static int hex_val(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return (c - 'a') + 10; }
    if (c >= 'A' && c <= 'F') { return (c - 'A') + 10; }
    return -1;
}

/**
 * Decode hex from @p src until a non-hex character (CR, LF or NUL in practice).
 *
 * @return decoded byte count, or 0 if the run was empty, had an odd number of
 *         digits, or overflowed @p cap.  An odd run is rejected rather than
 *         truncated: it means the reply was cut short, and half a message
 *         delivered as if whole is worse than no message.
 */
static size_t hex_decode(const char *src, uint8_t *dst, size_t cap)
{
    size_t n = 0U;

    while (true)
    {
        int hi = hex_val(src[0]);
        if (hi < 0)
        {
            break;      /* clean end of the run */
        }
        int lo = hex_val(src[1]);
        if (lo < 0)
        {
            EBADGE_WARN1("wifi8711 data: odd hex digit count near byte %u",
                         (unsigned)n);
            return 0U;
        }
        if (n >= cap)
        {
            EBADGE_WARN1("wifi8711 data: payload exceeds %u B cap",
                         (unsigned)cap);
            return 0U;
        }
        dst[n++] = (uint8_t)((hi << 4) | lo);
        src += 2;
    }
    return n;
}

/*----------------------------------------------------------------------------*
 *  Reply parsing
 *----------------------------------------------------------------------------*/

/** Advance to the start of the next line.  Handles CR, LF and CRLF, because the
 *  8711 emits CRLF but its own documentation shows bare LF in places. */
static const char *next_line(const char *p)
{
    while (*p != '\0' && *p != '\r' && *p != '\n') { p++; }
    while (*p == '\r' || *p == '\n')               { p++; }
    return p;
}

/** True if @p line starts with @p key; @p out_val then points just past it. */
static bool line_key(const char *line, const char *key, const char **out_val)
{
    size_t n = strlen(key);

    if (strncmp(line, key, n) != 0)
    {
        return false;
    }
    *out_val = line + n;
    return true;
}

/**
 * Find the DATA= run in a WLRECV reply and decode it.
 *
 * Expected shape, though only the DATA line is required:
 *
 *     [+WLRECV]:OK
 *     LEN=3
 *     DATA=A1B2C3
 *
 * LEN is cross-checked when present but is not trusted over the hex itself: the
 * hex is the payload, LEN is the 8711's opinion about it, and a mismatch means
 * the reply is malformed rather than that the payload should be resized.
 *
 * @return decoded byte count; 0 means "nothing pending", which is a normal
 *         answer and not an error.
 */
static size_t parse_recv(const char *text, uint8_t *out, size_t cap)
{
    const char *line = text;
    const char *val;
    long        want_len = -1;
    size_t      n        = 0U;

    while (*line != '\0')
    {
        if (line_key(line, "LEN=", &val))
        {
            want_len = 0;
            for (const char *p = val; *p >= '0' && *p <= '9'; p++)
            {
                want_len = (want_len * 10) + (*p - '0');
            }
        }
        else if (line_key(line, "DATA=", &val))
        {
            n = hex_decode(val, out, cap);
        }
        line = next_line(line);
    }

    if (want_len >= 0 && (size_t)want_len != n)
    {
        /* Report it and keep what decoded cleanly -- but say so, because this
         * is the signature of a reply truncated by our own text buffer, and
         * silently accepting a short message would hide that. */
        EBADGE_WARN2("wifi8711 data: LEN=%ld but decoded %u B",
                     want_len, (unsigned)n);
    }
    return n;
}

/*----------------------------------------------------------------------------*
 *  Completion
 *----------------------------------------------------------------------------*/

/** Detach and run the done callback.  Detaching first means a callback that
 *  immediately issues the next poll finds the slot free rather than -EBUSY. */
static void data_complete(bool ok)
{
    wifi_8711_data_done_cb_t cb   = s_done_cb;
    void                    *user = s_done_user;

    s_done_cb   = NULL;
    s_done_user = NULL;

    if (!ok)
    {
        s_stats.errors++;
    }
    if (cb != NULL)
    {
        cb(ok, user);
    }
}

/** AT-layer completion for both commands. */
static void on_at_reply(wifi_8711_at_result_t res, const char *text, size_t len,
                        void *user)
{
    bool is_recv = (user != NULL);

    ARG_UNUSED(len);

    if (res == WIFI_8711_AT_ERR_PEER)
    {
        /* "[AT]:ERROR" -- the firmware does not know this command.  Latch it so
         * a periodic caller stops burning a 2..4 s single-flight transaction
         * per period to be told the same thing forever.  Expected against
         * current firmware, so INFO rather than a warning: it is the documented
         * state of the peer, not a fault on this side. */
        s_supported = false;
        EBADGE_LOG("wifi8711 data: 8711 rejected the command -- tunnel not"
                   " implemented in this firmware, disabling further attempts");
        data_complete(false);
        return;
    }

    if (res != WIFI_8711_AT_OK && res != WIFI_8711_AT_ERR_TRUNC)
    {
        EBADGE_WARN1("wifi8711 data: transaction failed (%s)",
                     wifi_8711_at_result_str(res));
        data_complete(false);
        return;
    }

    if (!is_recv)
    {
        /* Send: there is nothing to parse.  The reply only tells us the 8711
         * accepted the message -- not that any peer received it. */
        data_complete(true);
        return;
    }

    /* TRUNC is worth refusing here, unlike in the AP layer.  There the
     * truncation eats the tail (the CLIENT list) and the useful fields survive;
     * here the tail IS the payload, so a truncated reply means a truncated
     * message. */
    if (res == WIFI_8711_AT_ERR_TRUNC)
    {
        EBADGE_WARN("wifi8711 data: reply truncated -- message dropped rather"
                    " than delivered short");
        data_complete(false);
        return;
    }

    uint8_t buf[WIFI_8711_AT_DATA_RECV_MAX];
    size_t  n = parse_recv(text, buf, sizeof(buf));

    if (n == 0U)
    {
        /* Normal and common: we asked, nothing had arrived.  Counted separately
         * so "the tunnel is idle" and "the tunnel is broken" are distinguishable
         * in the stats. */
        s_stats.empty_polls++;
        data_complete(true);
        return;
    }

    s_stats.rx_msgs++;
    s_stats.rx_bytes += (uint32_t)n;
    EBADGE_LOG1("wifi8711 data: %u B in from Wi-Fi", (unsigned)n);

    if (s_rx_cb != NULL)
    {
        s_rx_cb(buf, n, s_rx_user);
    }
    else
    {
        /* Say so loudly: silently discarding a message that did arrive would
         * look exactly like the 8711 never sending one. */
        EBADGE_WARN("wifi8711 data: no rx sink installed -- message DISCARDED");
        EBADGE_LOG_HEX("wifi8711 data rx", buf, n);
    }
    data_complete(true);
}

/*----------------------------------------------------------------------------*
 *  Submit helper
 *
 *  @param  is_recv  passed through the AT layer's user pointer, which is the
 *                   only per-transaction context available -- s_done_cb cannot
 *                   carry it because a caller may pass a NULL callback.
 *----------------------------------------------------------------------------*/
static int data_submit(const char *cmd, bool is_recv,
                       wifi_8711_data_done_cb_t cb, void *user)
{
    int rc;

    if (!s_supported)
    {
        /* Refused locally, without touching the wire.  The whole point of the
         * latch: the AT layer is single flight and port_softap needs it. */
        return -ENOTSUP;
    }
    if (s_done_cb != NULL)
    {
        return -EBUSY;
    }

    /* Record the caller BEFORE submitting: on a link that is already streaming,
     * the AT layer can complete before submit() returns, and a callback that
     * found s_done_cb empty would drop the answer. */
    s_done_cb   = cb;
    s_done_user = user;

    rc = wifi_8711_at_submit(cmd, on_at_reply, is_recv ? (void *)1 : NULL);
    if (rc != 0)
    {
        s_done_cb   = NULL;
        s_done_user = NULL;
    }
    return rc;
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
void wifi_8711_at_data_set_rx_sink(wifi_8711_data_rx_cb_t cb, void *user)
{
    s_rx_cb   = cb;
    s_rx_user = user;
}

int wifi_8711_at_data_poll(wifi_8711_data_done_cb_t cb, void *user)
{
    int rc = data_submit(WIFI_8711_AT_CMD_WLRECV, true, cb, user);

    if (rc == 0)
    {
        s_stats.polls++;
    }
    return rc;
}

int wifi_8711_at_data_send(const uint8_t *data, size_t len,
                           wifi_8711_data_done_cb_t cb, void *user)
{
    if (data == NULL || len == 0U || len > WIFI_8711_AT_DATA_SEND_MAX)
    {
        /* Rejected here rather than truncated.  A message the 8711 silently
         * drops for being too long is indistinguishable from one it never
         * received, so the caller has to be told now. */
        EBADGE_WARN2("wifi8711 data: send len=%u rejected (cap %u)",
                     (unsigned)len, (unsigned)WIFI_8711_AT_DATA_SEND_MAX);
        return -EMSGSIZE;
    }

    /* Frame in place: "AT+WLSEND=" + hex + CRLF.  BUILD_ASSERT above proves the
     * result fits both s_send_cmd and the 8711's parse buffer. */
    size_t pos = sizeof(WIFI_8711_AT_CMD_WLSEND_PREFIX) - 1U;
    memcpy(s_send_cmd, WIFI_8711_AT_CMD_WLSEND_PREFIX, pos);
    hex_encode(data, len, &s_send_cmd[pos]);
    pos += len * 2U;
    s_send_cmd[pos++] = '\r';
    s_send_cmd[pos++] = '\n';
    s_send_cmd[pos]   = '\0';

    int rc = data_submit(s_send_cmd, false, cb, user);
    if (rc == 0)
    {
        s_stats.sends++;
        s_stats.tx_bytes += (uint32_t)len;
        EBADGE_LOG1("wifi8711 data: %u B out to Wi-Fi (staged)", (unsigned)len);
    }
    return rc;
}

bool wifi_8711_at_data_supported(void)
{
    return s_supported;
}

void wifi_8711_at_data_get_stats(wifi_8711_data_stats_t *out)
{
    if (out != NULL)
    {
        *out = s_stats;
    }
}

#else  /* !CONFIG_WIFI_8711 */

/* Deliberately empty, and NOT a set of -ENODEV stubs.
 *
 * app/wifi_8711/CMakeLists.txt wraps the whole directory in
 * if(CONFIG_WIFI_8711), so this branch is never compiled and a stub here could
 * not help anyone -- it would only look as though callers were free to drop
 * their own guards.  They are not: the include path for this header is also
 * inside that if(), so a caller in app/protocol/ must wrap both the #include
 * and the call in #if defined(CONFIG_WIFI_8711), exactly as cmd_debug.c and
 * ebadge_port_softap.c already do. */

#endif /* CONFIG_WIFI_8711 */
