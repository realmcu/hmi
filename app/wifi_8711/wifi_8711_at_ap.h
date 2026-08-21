/**
 * @file    wifi_8711_at_ap.h
 * @brief   SoftAP semantics on top of the ATMC transaction layer.
 *
 * This is the layer that knows what "AT+WLSTATE" MEANS.  It turns the 8711's
 * line-oriented reply into a struct, caches it, and exposes the two operations
 * the protocol stack actually needs: "make sure the AP is up" and "tell me its
 * credentials".
 *
 * ---------------------------------------------------------------------------
 * THE ONE THING TO UNDERSTAND: WE DO NOT OWN THE ACCESS POINT
 * ---------------------------------------------------------------------------
 * The 8711 raises its own SoftAP at boot and runs its own TCP server on port
 * 5004 (see note/refer/phone-to-8711-jpeg-tcp-protocol.md sec.1/sec.2).  Its AT
 * firmware has exactly two commands and NEITHER of them sets anything:
 *
 *   AT+WLSTARTAP   ensure the AP is running, read back SSID + password
 *   AT+WLSTATE     read SSID / password / IP / port / channel / assoc clients
 *
 * So this side cannot choose the SSID, the password, the channel or the port.
 * It can only ask what they already are.  Any code that invents credentials and
 * expects the phone to find that network is wrong -- the phone must be told the
 * 8711's real ones, which is what wifi_8711_at_ap_info() is for.
 *
 * That also means "start the AP" is really "confirm the AP" and is idempotent.
 *
 * ---------------------------------------------------------------------------
 * WHY THERE IS A CACHE
 * ---------------------------------------------------------------------------
 * Every query costs 2..4 s on an idle link, and the BLE control plane needs the
 * credentials inside a command handler that must not block.  So a successful
 * reply is cached and served synchronously; callers that need certainty ask for
 * a refresh and get called back.  The credentials are static in practice (the
 * 8711 has no way to change them), which is what makes the cache safe -- the
 * volatile field is the client list, and that is never served from cache.
 */
#ifndef _WIFI_8711_AT_AP_H_
#define _WIFI_8711_AT_AP_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** SoftAP description as reported by the 8711.
 *
 *  Field widths follow the 802.11 / protocol maxima rather than what the
 *  current firmware happens to emit, so a longer SSID in a future build cannot
 *  overflow anything here. */
typedef struct
{
    char     ssid[33];      /**< NUL-terminated, up to 32 B                    */
    char     password[64];  /**< NUL-terminated; "" means an open network      */
    uint32_t ip;            /**< AP-side IPv4, HOST order (0xC0A82B01)         */
    uint16_t port;          /**< the 8711's TCP data port (5004 in practice)   */
    uint8_t  channel;       /**< 2.4 GHz channel, or 0 if the reply omits it   */
    uint8_t  clients;       /**< associated STAs right now                     */
    bool     running;       /**< the reply described a live AP                 */
} wifi_8711_ap_info_t;

/**
 * @brief  Completion of an AP query.
 *
 * @param  ok    true if @p info is populated from a fresh reply
 * @param  info  never NULL; all-zero when @p ok is false
 * @param  user  the pointer handed to the request
 *
 * CONTEXT: transport thread or system workqueue -- NOT the protocol stack's
 * l2_task.  Anything touching protocol state must marshal itself over with
 * ebadge_task_post_call().
 */
typedef void (*wifi_8711_ap_cb_t)(bool ok, const wifi_8711_ap_info_t *info,
                                  void *user);

/*----------------------------------------------------------------------------*
 *  Queries
 *----------------------------------------------------------------------------*/

/**
 * @brief  Query AP state ("AT+WLSTATE") and update the cache.
 *
 * Asynchronous: 0 means "queued", not "answered".  @p cb fires in ~2..4 s on an
 * idle link, in milliseconds while a JPEG stream is running.
 *
 * @retval 0        queued
 * @retval -EBUSY   another AT command is outstanding (single flight)
 * @retval -ENODEV  no 8711 link on this build / init never succeeded
 * @retval <0       staging error
 */
int wifi_8711_at_ap_query(wifi_8711_ap_cb_t cb, void *user);

/**
 * @brief  Ensure the AP is up ("AT+WLSTARTAP") and cache what it reports.
 *
 * Idempotent on the 8711 side, and its AP self-starts at boot, so this is a
 * confirmation rather than a state change.  Its reply carries only SSID and
 * password -- no IP, port or client count -- so a caller that needs those must
 * follow up with wifi_8711_at_ap_query().  Same asynchronous contract.
 */
int wifi_8711_at_ap_start(wifi_8711_ap_cb_t cb, void *user);

/*----------------------------------------------------------------------------*
 *  Cache
 *----------------------------------------------------------------------------*/

/**
 * @brief  Read the last successfully parsed AP info without going on the wire.
 *
 * For synchronous callers such as BLE command handlers, which cannot wait
 * seconds for a reply.
 *
 * @param  out  filled in only when this returns true
 * @return true if a query has ever succeeded; false if nothing is known yet
 *
 * NOTE the `clients` field is a snapshot from whenever that query ran and may
 * be arbitrarily stale.  SSID / password / IP / port / channel are safe to
 * trust -- the 8711 has no mechanism to change them -- but never decide "the
 * phone has connected" from a cached client count.
 */
bool wifi_8711_at_ap_cached(wifi_8711_ap_info_t *out);

/** Age of the cache in ms, or UINT32_MAX if it has never been populated. */
uint32_t wifi_8711_at_ap_cache_age_ms(void);

/*----------------------------------------------------------------------------*
 *  Parser -- exposed for unit-testing against captured replies
 *----------------------------------------------------------------------------*/

/**
 * @brief  Parse a WLSTATE / WLSTARTAP reply body into @p out.
 *
 * Accepts both shapes, since they overlap: WLSTATE answers
 * "SSID= / PASSWORD= / IP= / PORT= / CHANNEL= / FILE_PORT= / CLIENTS= /
 * CLIENT=..." terminated by "[+WLSTATE]:OK", while WLSTARTAP answers
 * "[+WLSTARTAP]:OK" followed by just SSID and PASSWORD.  Fields that are absent
 * are left zeroed, so a caller must check what it needs rather than assuming a
 * full struct -- CHANNEL= only appears from the v2.1 8711 firmware onwards.
 *
 * @return true if at least an SSID was found and the reply was not [AT]:ERROR
 */
bool wifi_8711_at_ap_parse(const char *text, wifi_8711_ap_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_8711_AT_AP_H_ */
