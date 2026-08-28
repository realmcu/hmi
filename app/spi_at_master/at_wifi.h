/**
 * @file    at_wifi.h
 * @brief   The AT+WL* commands eBadge uses, plus the AP state that follows from
 *          having issued them.  One of the two semantic layers over at_client.
 *
 * ---------------------------------------------------------------------------
 * WE CONFIGURE THE AP NOW.  WE DO NOT DISCOVER IT
 * ---------------------------------------------------------------------------
 * The old in-house firmware derived the SSID and password from the 8711's own
 * MAC and pushed a state block up about once a second, so the AP was something
 * this side READ.  With the standard command set the credentials are arguments to
 * AT+WLSTARTAP, which means the authoritative copy of "what the AP is" lives
 * here, in RAM, written before the command went out.
 *
 * That removes the freshness problem rather than solving it: there is no feed to
 * go stale, and AT+WLSTATE is no longer where the answers come from.
 *
 * ---------------------------------------------------------------------------
 * "OK" MEANS THE RADIO IS UP.  THAT IS NOT THE USUAL AT CONTRACT
 * ---------------------------------------------------------------------------
 * at_wlstartap does not return once the request is accepted.  It calls
 * wifi_start_ap() and then polls wifi_get_setting() once a second, comparing the
 * running SSID against the one asked for, for up to 20 iterations; only then does
 * it start the DHCP server and print OK.  A timeout prints
 * ERROR: <RTW_AT_ERR_SAP_START_FAILED> instead.
 *
 * Two consequences, and the first is easy to get wrong:
 *
 *   - the command can occupy the single-flight AT layer for over 20 seconds, so
 *     it needs AT_WIFI_AP_START_TIMEOUT_MS rather than at_client's default 3 s.
 *     A default-timeout submission would report AT_RES_TIMEOUT while the 8711 is
 *     still working, and the OK would then arrive with no command in flight.
 *   - there is no STARTING state to model.  A caller learns the outcome once, in
 *     the completion callback, and "the AP is coming up" is exactly the interval
 *     during which that callback has not fired.
 *
 * ---------------------------------------------------------------------------
 * THERE ARE NO ASSOCIATION EVENTS
 * ---------------------------------------------------------------------------
 * atcmd_wifi.c contains not one at_printf_indicate() call: the standard firmware
 * never emits [$]client connected, [$]assign client ip, or anything like them.
 * Only sockets, MQTT, OTA and websocket produce [$] lines at all.
 *
 * So this module cannot tell anybody that a phone has joined, and does not try.
 * The client count is available by polling AT+WLSTATE (at_wifi_ap_query(), which
 * parses the "Client Num:" line), but the joined EDGE belongs to at_socket: the
 * [$][SKT][EVENT] seed-connect indication is event-driven, and "a phone opened a
 * TCP connection to us" is closer to what a session actually waits for than "a
 * station associated at layer 2" ever was.
 *
 * CONTEXT: submit from any thread.  Callbacks arrive on at_client's threads --
 * the SPI transport thread or the system workqueue, never l2_task.
 */
#ifndef _AT_WIFI_H_
#define _AT_WIFI_H_

#include <stdint.h>
#include <stdbool.h>

#include "at_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Budget for AT+WLSTARTAP.
 *
 *  The handler polls for the AP to come up 20 times at 1 s intervals before
 *  giving up, and only then prints its own ERROR -- so anything under ~21 s would
 *  make us report a timeout for a command the 8711 is still executing, and the
 *  late OK would land with nothing in flight to consume it.  25 s leaves room for
 *  wifi_start_ap() itself and for the DHCP server start that follows the loop. */
#define AT_WIFI_AP_START_TIMEOUT_MS   25000U

/** Budget for AT+WLSTOPAP.  It has no error path at all -- dhcps_deinit(),
 *  wifi_stop_ap(), OK -- so this only has to cover the radio actually stopping. */
#define AT_WIFI_AP_STOP_TIMEOUT_MS     8000U

/** Budget for AT+WLSTATE.  Reads driver state and prints; no waiting. */
#define AT_WIFI_STATE_TIMEOUT_MS       3000U

/** SSID buffer size, matching ebadge_softap_info_t.  32 chars + NUL. */
#define AT_WIFI_SSID_MAX    33
/** Password buffer size.  The 8711 accepts [8,64] characters for WPA2. */
#define AT_WIFI_PW_MAX      65

/*----------------------------------------------------------------------------*
 *  What we told the 8711 to be
 *----------------------------------------------------------------------------*/
typedef struct
{
    char     ssid[AT_WIFI_SSID_MAX];
    char     password[AT_WIFI_PW_MAX];
    uint8_t  channel;
    bool     up;            /**< an AT+WLSTARTAP has completed with OK          */
    bool     starting;      /**< a start is on the wire; up is not yet meaningful */
} at_wifi_ap_t;

/**
 * @brief  Outcome of one AT+WLSTARTAP / AT+WLSTOPAP.
 *
 * @param  ok        the command answered OK.  For a start this means the radio is
 *                   genuinely on the air with the SSID that was asked for.
 * @param  error_no  the <n> from "ERROR: <n>", else 0.  Worth logging: 20 s of
 *                   polling that never saw our SSID reports
 *                   RTW_AT_ERR_SAP_START_FAILED, which is a different fault from
 *                   a password the 8711 rejected outright.
 */
typedef void (*at_wifi_done_cb_t)(bool ok, int error_no, void *user);

/*----------------------------------------------------------------------------*
 *  Lifecycle
 *----------------------------------------------------------------------------*/

/**
 * @brief  Initialise the module and the AT client under it.
 *
 * Idempotent.  Calls at_client_init(), so a caller does not need to.
 */
int at_wifi_init(void);

/*----------------------------------------------------------------------------*
 *  SoftAP
 *----------------------------------------------------------------------------*/

/**
 * @brief  Bring up the SoftAP with our credentials.
 *
 * Sends AT+WLSTARTAP=ssid,<ssid>,pw,<pw>,sec,wpa2[,ch,<ch>].  The arguments are
 * order-insensitive key,value pairs on the wire; this builds them in a fixed
 * order for readability, not because the 8711 cares.
 *
 * @param  ssid     1..32 characters
 * @param  password 8..64 characters -- the 8711 rejects anything else with
 *                  RTW_AT_ERR_INVALID_PARAM_VALUE, so it is checked here too
 * @param  channel  1..14, or 0 to omit the key and let the 8711 choose
 * @param  done     called exactly once, possibly more than 20 s later
 *
 * @retval 0         queued
 * @retval -EINVAL   credentials outside the ranges above
 * @retval -EALREADY a start is already on the wire
 * @retval other     as at_client_submitf()
 */
int at_wifi_ap_start(const char *ssid, const char *password, uint8_t channel,
                     at_wifi_done_cb_t done, void *user);

/**
 * @brief  Take the SoftAP down.  Sends AT+WLSTOPAP.
 *
 * Marks the cached AP down as soon as the command completes, whatever it says:
 * the handler has no failure path, so an outcome other than OK means the link
 * broke rather than that the radio survived.
 */
int at_wifi_ap_stop(at_wifi_done_cb_t done, void *user);

/**
 * @brief  What we believe the AP to be.  Never blocks, never puts anything on
 *         the wire.
 *
 * Safe to call from l2_task, which is the point: a BLE handler answering a 0x13
 * offer needs the credentials in microseconds.  The struct is a copy taken under
 * a lock, so the caller may keep it.
 */
void at_wifi_ap_get(at_wifi_ap_t *out);

/** Shorthand for at_wifi_ap_get(&x).up -- an AT+WLSTARTAP has said OK and no
 *  stop has completed since. */
bool at_wifi_ap_up(void);

/*----------------------------------------------------------------------------*
 *  AT+WLSTATE -- confirmation, not a source of truth
 *----------------------------------------------------------------------------*/

/**
 * @brief  Parsed subset of AT+WLSTATE's human-readable output.
 *
 * The reply is column-aligned "KEY => VALUE" text meant for a person, with a
 * varying number of leading spaces per key and no port information anywhere.
 * Only the two facts we cannot already know are extracted; the credentials are
 * deliberately not parsed back, because we chose them.
 */
typedef struct
{
    bool     mode_ap;       /**< a "MODE => AP" line was present                */
    bool     running;       /**< a "WLAN<n> Status: Running" line was present    */
    uint8_t  clients;       /**< from "Client Num: <n>", 0 if absent             */
} at_wifi_state_t;

/** @brief  Outcome of one AT+WLSTATE, with whatever was parsed out of it. */
typedef void (*at_wifi_state_cb_t)(bool ok, const at_wifi_state_t *st,
                                   void *user);

/**
 * @brief  Ask the 8711 to describe its Wi-Fi state.
 *
 * Used for two things only: confirming after a start that the radio really is an
 * AP ("MODE => AP"), and reading the associated client count when something wants
 * it without waiting for a socket event.  Everything else in the reply is
 * discarded.
 *
 * Not called on a timer anywhere.  There is no state feed to keep alive now, so a
 * periodic WLSTATE would be a poll we invented rather than one we inherited.
 */
int at_wifi_ap_query(at_wifi_state_cb_t done, void *user);

#ifdef __cplusplus
}
#endif

#endif /* _AT_WIFI_H_ */
