/**
 * @file    wifi_8711_at_query.c
 * @brief   Bring-up hook: fire one AT command and log what the 8711 says.
 *
 * This used to own its own slot sink, sequence matching and TX-idle restore --
 * all of which is now wifi_8711_at.c's job, and having two sinks competing for
 * one transport meant whichever ran last silently stole the other's replies.
 * What is left here is the debug *presentation*: the same information a human
 * wants at the console, printed from the parsed struct rather than re-derived
 * from the raw text.
 *
 * See the header for why this cannot be synchronous.  The short version: the
 * 8711 owns the clock, so a caller can only stage and the reply lands on the
 * transport thread a couple of polls later.
 */
#include <zephyr/kernel.h>
#include <errno.h>
#include <stdbool.h>
#include "wifi_8711_at_query.h"
#include "wifi_8711_at.h"
#include "wifi_8711_at_ap.h"
#include "../protocol/ebadge_log.h"

#if defined(CONFIG_WIFI_8711)

/** Print the parsed AP state from a WLSTATE reply.  Runs on the AT layer's
 *  callback context. */
static void query_done(bool ok, const wifi_8711_ap_info_t *info, void *user)
{
    const char *what = (const char *)user;

    if (!ok)
    {
        /* The AT layer has already logged the specific reason (timeout, peer
         * [AT]:ERROR, parse failure), so this only has to say which command it
         * belonged to -- repeating the reason would double every failure. */
        EBADGE_WARN1("wifi8711 %s: no usable AP state", what);
        return;
    }

    EBADGE_LOG("---- 8711 SoftAP state ----");
    /* The AP= line first, because it decides what the rest means: everything
     * below is zero unless the state is UP (sec.7.4 expresses "no value" as a
     * zero value), so an ssid="" under AP=DOWN is the expected reading rather
     * than a fault to go looking for. */
    EBADGE_LOG2("%s: state=%s", what, wifi_8711_ap_state_str(info->state));
    EBADGE_LOG1("ssid=\"%s\"", info->ssid);
    /* The password is printed deliberately: it is a fixed vendor constant that
     * the 8711 prints in its own boot log, and the whole point of this hook is
     * to confirm the phone will be told the right one. */
    EBADGE_LOG1("password=\"%s\"", info->password);
    EBADGE_LOG4("ip=%u.%u.%u.%u",
                (unsigned)((info->ip >> 24) & 0xFFU),
                (unsigned)((info->ip >> 16) & 0xFFU),
                (unsigned)((info->ip >>  8) & 0xFFU),
                (unsigned)(info->ip & 0xFFU));
    /* Both ports, labelled by what they accept.  Printing one "port=" was fine
     * while the struct had one field; it stopped being fine when the 8711 turned
     * out to run two servers with incompatible admission rules, and a console
     * that showed only one of them could not be used to check the other. */
    EBADGE_LOG2("stream_port=%u (raw JPEG)  file_port=%u (EBXF)",
                (unsigned)info->stream_port, (unsigned)info->file_port);
    EBADGE_LOG2("channel=%u clients=%u", (unsigned)info->channel,
                (unsigned)info->clients);
    EBADGE_LOG("---------------------------");
}

/** Print the outcome of a start.  Runs on the AT layer's callback context.
 *
 *  Deliberately NOT query_done(): a start's reply is a verdict, so the only field
 *  of the info struct it fills is `state` (wifi_8711_at_ap.h).  Printing the
 *  credential block from it would show ssid="" and port=0 after a *successful*
 *  start and read as "the AP has no SSID" -- which is exactly the wrong thing for
 *  a hook whose job is to make the link's state legible. */
static void start_done(bool ok, const wifi_8711_ap_info_t *info, void *user)
{
    ARG_UNUSED(user);

    if (!ok)
    {
        /* wifi_8711_at_ap.c has already said why. */
        EBADGE_WARN("wifi8711 WLSTARTAP: not confirmed");
        return;
    }

    /* "Accepted", not "up" -- and the state says which of the two this was.  This
     * line used to claim the radio was up on any OK, which is wrong for the
     * common case: a real bring-up takes seconds to tens of seconds and the 8711
     * answers immediately so as not to freeze the SPI link while it runs.
     *
     * Nobody needs to follow up with WLSTATE either.  Since sec.7.1 the state
     * block arrives on the POLL beat about once a second, so the credentials show
     * up on their own -- run `wifi 8711 at state` only to see them sooner. */
    EBADGE_LOG1("wifi8711 WLSTARTAP: request accepted, state=%s (credentials"
                " arrive on the ~1 Hz state beat)",
                wifi_8711_ap_state_str(info->state));
}

int wifi_8711_at_query_ap_info(void)
{
    return wifi_8711_at_ap_query(query_done, "WLSTATE");
}

int wifi_8711_at_start_ap(void)
{
    return wifi_8711_at_ap_start(start_done, NULL);
}

#endif /* CONFIG_WIFI_8711 */
