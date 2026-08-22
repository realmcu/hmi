/**
 * @file    wifi_8711_at_xfer.c
 * @brief   AT+XFERACK / AT+XFERSTOP formatting and reply classification.
 *
 * See the header for why the 8711 no longer decides a transfer's outcome.  What
 * is here is only the two things this layer owes: format the command text, and
 * say whether the 8711 agreed.
 *
 * The reply check is a plain substring search, unlike wifi_8711_at_ap.c's
 * line-anchored parser.  The difference is justified: there is no field to
 * extract here, only a verdict, and the two possible lines ("[+XFERACK]:OK" and
 * "[AT]:ERROR") cannot occur inside anything else this command returns -- there
 * is no payload for them to hide in.
 */
#include <zephyr/kernel.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wifi_8711.h"
#include "wifi_8711_at.h"
#include "wifi_8711_at_xfer.h"
#include "spi_at_protocol.h"
#include "../protocol/ebadge_log.h"

#if defined(CONFIG_WIFI_8711)

/* "AT+XFERACK=" + "255" + "," + "255" + "\r\n" + NUL.  Sized from the parts
 * rather than guessed, and checked below, so editing the prefix cannot silently
 * produce a truncated command -- the 8711 drops an over-long or malformed
 * COMMAND without saying anything, which is the worst failure to debug. */
#define XFER_ACK_CMD_MAX  (sizeof(SPI_AT_CMD_XFERACK_PREFIX) - 1U + 3U + 1U + 3U + 2U + 1U)

BUILD_ASSERT(XFER_ACK_CMD_MAX <= WIFI_8711_AT_COMMAND_MAX,
             "AT+XFERACK command exceeds the 8711's 128 B parse buffer");
BUILD_ASSERT(sizeof(SPI_AT_CMD_XFERSTOP) <= WIFI_8711_AT_COMMAND_MAX,
             "AT+XFERSTOP command exceeds the 8711's 128 B parse buffer");

static wifi_8711_at_xfer_stats_t s_stats;

/* Which command a reply belongs to, so the log line names the right one.  Kept
 * as a static rather than passed through `user`, because `user` belongs to the
 * caller -- xfer_session uses it -- and there is only ever one command in
 * flight (the AT layer is single flight). */
static const char *s_pending_what = "XFER";

/*----------------------------------------------------------------------------*
 *  Reply handling
 *----------------------------------------------------------------------------*/
typedef struct
{
    wifi_8711_at_xfer_done_cb_t cb;
    void                    *user;
} xfer_req_t;

/* One request in flight, for the same single-flight reason as s_pending_what. */
static xfer_req_t s_req;

static void xfer_reply(wifi_8711_at_result_t res, const char *text, size_t len,
                       void *user)
{
    (void)user;
    (void)len;

    bool ok = false;

    if (res != WIFI_8711_AT_OK)
    {
        /* The AT layer has already logged the specific reason (timeout, peer
         * refusal, truncation), so naming the command is all this adds. */
        EBADGE_WARN2("wifi8711 %s: transaction failed (%s)", s_pending_what,
                     wifi_8711_at_result_str(res));
    }
    else if (text != NULL && strstr(text, "[AT]:ERROR") != NULL)
    {
        /* The 8711 answers ERROR when there is no active file connection --
         * which is the expected answer if it already hit its own 120 s wait, or
         * if the phone closed first.  Worth a line: it means the phone did NOT
         * get the EBXR we tried to send. */
        EBADGE_WARN1("wifi8711 %s: refused -- no active file session?",
                     s_pending_what);
    }
    else if (text != NULL && strstr(text, "]:OK") != NULL)
    {
        ok = true;
    }
    else
    {
        EBADGE_WARN1("wifi8711 %s: unrecognised reply", s_pending_what);
    }

    if (ok) { s_stats.confirmed++; }
    else { s_stats.errors++; }

    wifi_8711_at_xfer_done_cb_t cb = s_req.cb;
    void                    *cb_user = s_req.user;
    s_req.cb   = NULL;
    s_req.user = NULL;
    if (cb != NULL)
    {
        cb(ok, cb_user);
    }
}

/*----------------------------------------------------------------------------*
 *  API
 *----------------------------------------------------------------------------*/
int wifi_8711_at_xfer_ack(uint8_t status, uint8_t reason,
                          wifi_8711_at_xfer_done_cb_t cb, void *user)
{
    /* Spec sec.10.3: reason must be 0 when status is success.  Refused locally
     * rather than sent, because the 8711 would put both bytes in the EBXR
     * verbatim and the App would receive "succeeded, because it failed". */
    if (status == WIFI_8711_XFER_STATUS_OK && reason != 0U)
    {
        EBADGE_WARN1("wifi8711 XFERACK: success with reason=%u refused",
                     (unsigned)reason);
        return -EINVAL;
    }

    char cmd[XFER_ACK_CMD_MAX];
    int  n = snprintf(cmd, sizeof(cmd), "%s%u,%u\r\n",
                      SPI_AT_CMD_XFERACK_PREFIX,
                      (unsigned)status, (unsigned)reason);
    if (n < 0 || (size_t)n >= sizeof(cmd))
    {
        /* Unreachable given the BUILD_ASSERTs, but a truncated AT command is
         * exactly the silent-drop case, so do not hand one over. */
        return -EMSGSIZE;
    }

    s_pending_what = "XFERACK";
    s_req.cb   = cb;
    s_req.user = user;

    int rc = wifi_8711_at_submit(cmd, xfer_reply, NULL);
    if (rc != 0)
    {
        s_req.cb   = NULL;
        s_req.user = NULL;
        return rc;
    }

    if (status == WIFI_8711_XFER_STATUS_OK) { s_stats.acks_ok++; }
    else                                    { s_stats.acks_fail++; }

    EBADGE_LOG2("wifi8711 XFERACK: status=%u reason=%u staged",
                (unsigned)status, (unsigned)reason);
    return 0;
}

int wifi_8711_at_xfer_stop(wifi_8711_at_xfer_done_cb_t cb, void *user)
{
    s_pending_what = "XFERSTOP";
    s_req.cb   = cb;
    s_req.user = user;

    int rc = wifi_8711_at_submit(SPI_AT_CMD_XFERSTOP, xfer_reply, NULL);
    if (rc != 0)
    {
        s_req.cb   = NULL;
        s_req.user = NULL;
        return rc;
    }
    s_stats.stops++;
    EBADGE_LOG("wifi8711 XFERSTOP: staged (no EBXR will be sent)");
    return 0;
}

void wifi_8711_at_xfer_get_stats(wifi_8711_at_xfer_stats_t *out)
{
    if (out != NULL)
    {
        *out = s_stats;
    }
}

#else /* !CONFIG_WIFI_8711 */

int wifi_8711_at_xfer_ack(uint8_t status, uint8_t reason,
                          wifi_8711_at_xfer_done_cb_t cb, void *user)
{
    (void)status; (void)reason; (void)cb; (void)user;
    return -ENODEV;
}

int wifi_8711_at_xfer_stop(wifi_8711_at_xfer_done_cb_t cb, void *user)
{
    (void)cb; (void)user;
    return -ENODEV;
}

void wifi_8711_at_xfer_get_stats(wifi_8711_at_xfer_stats_t *out)
{
    if (out != NULL)
    {
        memset(out, 0, sizeof(*out));
    }
}

#endif /* CONFIG_WIFI_8711 */
