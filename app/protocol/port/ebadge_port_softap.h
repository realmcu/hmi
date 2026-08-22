/**
 * @file    ebadge_port_softap.h
 * @brief   SoftAP control for the Wi-Fi data plane.
 *
 * ---------------------------------------------------------------------------
 * THIS IS A READ-BACK API, NOT A CONFIGURE API
 * ---------------------------------------------------------------------------
 * The backing radio is the RTL8711FA reached over SPI.  It raises its own
 * SoftAP at boot and runs its own TCP server, and its AT surface has exactly
 * two commands, NEITHER of which sets anything (see wifi_8711_at_ap.h).  So a
 * caller cannot choose the SSID, password, channel or port -- it can only ask
 * what they already are.
 *
 * That is why ebadge_port_softap_start() takes no credentials and hands them
 * back instead.  The earlier shape of this API accepted an
 * ebadge_softap_info_t to program, and both session state machines duly filled
 * one in with invented values ("eBadge-XFR" / 192.168.4.1:9000) and sent them
 * to the phone in 0x13 AP_INFO.  The phone would then look for a network that
 * does not exist.  Getting the struct the other way round is what makes that
 * mistake unrepresentable.
 *
 * ---------------------------------------------------------------------------
 * WHY start() CANNOT BLOCK, AND WHAT IT COSTS
 * ---------------------------------------------------------------------------
 * Confirming the AP is a round trip over a link the 8711 clocks on its own
 * schedule -- 2..4 s while idle.  start() is called from a BLE command handler
 * on l2_task, the single serialiser for the whole protocol stack, so it must
 * not wait.  It therefore answers from a cache, and fails with -EAGAIN if
 * nothing is known yet.  Callers should map that to "not ready, retry", never
 * to a hard failure.
 *
 * ---------------------------------------------------------------------------
 * NOTHING HERE POLLS WHILE IDLE
 * ---------------------------------------------------------------------------
 * The cache used to be kept warm by a periodic query, which on a board whose
 * 8711 never answers meant one AT round trip every 10 s for the life of the
 * device -- each one occupying a single-flight AT layer that real callers then
 * found -EBUSY.  It is now filled by a bounded burst of attempts after boot and
 * thereafter only on demand, so an idle device goes quiet on the AT link.
 *
 * The consequence for callers is that a cache miss is not guaranteed to fix
 * itself: something has to ask.  Both -EAGAIN from start() and a false from
 * info() provoke a query on the way out, so "retry later" is advice the device
 * is acting on -- but the retry is the caller's job, and a caller that gives up
 * after one miss will never see the credentials.
 */
#ifndef _EBADGE_PORT_SOFTAP_H_
#define _EBADGE_PORT_SOFTAP_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Callback: a STA has associated with the AP.  Delivered ON l2_task -- this
 *  module has already marshalled it, so a session handler may touch its own
 *  state directly.  Fires at most once per start()/stop() cycle.             */
typedef void (*ebadge_softap_sta_joined_cb_t)(void);

typedef struct
{
    char     ssid[33];        /* nul-terminated                              */
    char     password[64];    /* nul-terminated; "" for an open network       */
    uint8_t  channel;
    uint32_t ip;              /* AP-side IPv4 in host order, e.g. 0xC0A82B01 */
} ebadge_softap_info_t;

/*----------------------------------------------------------------------------*
 *  WHICH PORT A CALLER WANTS IS A SEMANTIC CHOICE, NOT A DETAIL
 *
 *  The 8711 runs two TCP servers and they accept different things: the preview
 *  port takes bare JPEG frames only, the file port takes an EBXF header plus
 *  body.  Sending a file to the preview port does not fail loudly -- the phone
 *  connects, the 8711 discards what it cannot parse, and the transfer dies on a
 *  timeout with nothing pointing at the cause.  That happened, which is why the
 *  API now makes the caller name the role instead of handing out "the port".
 *----------------------------------------------------------------------------*/
typedef enum
{
    EBADGE_AP_PORT_FILE = 0,   /**< EBXF file upload      (FILE_PORT=, 9000) */
    EBADGE_AP_PORT_STREAM,     /**< raw-JPEG preview      (PORT=,      5004) */
} ebadge_ap_port_role_t;

/*----------------------------------------------------------------------------*
 *  Lifecycle
 *----------------------------------------------------------------------------*/

/**
 * @brief  Register the tick sink and start filling the AP state cache.  Call
 *         once from ebadge_task_init().
 *
 * Costs a bounded handful of AT queries after boot and then nothing at all
 * while idle -- see "NOTHING HERE POLLS WHILE IDLE" above.  Safe to call before
 * the radio is up; the first attempt is expected to fail and is retried.
 */
void ebadge_port_softap_init(void);

/**
 * @brief  Ensure the AP is up and report what it actually is.
 *
 * Non-blocking.  Succeeds only if the credentials are already known, which is
 * the normal case: the cache is primed at boot and the values are static.
 *
 * @param  out_info   filled with the live SSID / password / channel / IP
 * @param  role       which of the 8711's two servers this session will use
 * @param  out_port   filled with the port for @p role
 * @param  joined_cb  invoked on l2_task when a STA associates; may be NULL
 *
 * @retval 0        AP confirmed; @p out_info and @p out_port are valid
 * @retval -EAGAIN  nothing cached yet -- a refresh is in flight, retry later
 */
int  ebadge_port_softap_start(ebadge_softap_info_t *out_info,
                              ebadge_ap_port_role_t role,
                              uint16_t *out_port,
                              ebadge_softap_sta_joined_cb_t joined_cb);

/**
 * @brief  Stop caring about the AP.
 *
 * Does NOT take the radio down: the 8711's AP is not ours to stop, and there
 * is no AT command that would.  It drops the joined-callback, which is what
 * ends the join poll -- with no session waiting, this module stops touching the
 * AT link.  Safe to call idempotently on cleanup paths.
 */
int  ebadge_port_softap_stop(void);

/** True between start() and stop() -- i.e. a session is relying on the AP.
 *  NOT "the radio is on"; the 8711's AP is up from boot either way.          */
bool ebadge_port_softap_running(void);

/**
 * @brief  Read the cached AP description without starting a session.
 *
 * For the 0x12 GET_AP_INFO handler, which must answer a phone that lost its
 * 0x13 without implying a transfer is in progress.
 *
 * A false return provokes a background query, so a caller that retries will
 * eventually succeed if the link works at all.  Non-blocking either way: the
 * answer to THIS call still comes from the cache.
 *
 * @param  role      which port to report -- see ebadge_ap_port_role_t
 * @param  out_port  the port for @p role; 0 if the 8711 has not reported it
 *
 * @return true if anything is known yet; false leaves the outputs untouched
 */
bool ebadge_port_softap_info(ebadge_softap_info_t *out_info,
                             ebadge_ap_port_role_t role,
                             uint16_t *out_port);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_SOFTAP_H_ */
