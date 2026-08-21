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

/** Print the parsed AP state.  Runs on the AT layer's callback context. */
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
    EBADGE_LOG2("%s: ssid=\"%s\"", what, info->ssid);
    /* The password is printed deliberately: it is a fixed vendor constant that
     * the 8711 prints in its own boot log, and the whole point of this hook is
     * to confirm the phone will be told the right one. */
    EBADGE_LOG1("password=\"%s\"", info->password);
    EBADGE_LOG4("ip=%u.%u.%u.%u",
                (unsigned)((info->ip >> 24) & 0xFFU),
                (unsigned)((info->ip >> 16) & 0xFFU),
                (unsigned)((info->ip >>  8) & 0xFFU),
                (unsigned)(info->ip & 0xFFU));
    EBADGE_LOG2("port=%u clients=%u", (unsigned)info->port,
                (unsigned)info->clients);
    EBADGE_LOG("---------------------------");
}

int wifi_8711_at_query_ap_info(void)
{
    return wifi_8711_at_ap_query(query_done, "WLSTATE");
}

int wifi_8711_at_start_ap(void)
{
    return wifi_8711_at_ap_start(query_done, "WLSTARTAP");
}

#endif /* CONFIG_WIFI_8711 */
