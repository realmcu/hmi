/**
 * @file    ebadge_port_softap.h
 * @brief   SoftAP control for the Wi-Fi data plane.
 *
 * ---------------------------------------------------------------------------
 * THIS IS A READ-BACK API, NOT A CONFIGURE API
 * ---------------------------------------------------------------------------
 * The backing radio is the RTL8711FA reached over SPI.  It runs its own SoftAP
 * and its own TCP servers, and its AT surface offers no way to configure any of
 * that (see wifi_8711_at_ap.h): a caller cannot choose the SSID, password,
 * channel or port -- it can only ask what they already are.  What it CAN do is
 * switch the radio on (ebadge_port_softap_arm()) and off again
 * (ebadge_port_softap_shutdown()).
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
 * THE ANSWERS ARE ALREADY HERE -- NOTHING BLOCKS AND NOTHING POLLS
 * ---------------------------------------------------------------------------
 * The 8711 describes its Wi-Fi to us about once a second whether we ask or not
 * (protocol v3.1 sec.7.1 put the state block in the POLL payload), and
 * wifi_8711_at_ap.c keeps the one copy of it.  So every reader on this surface
 * answers from that copy, in microseconds, on l2_task -- which matters because
 * l2_task is the single serialiser for the whole protocol stack and an AT round
 * trip costs ~10 s.
 *
 * This module therefore has no periodic query at all.  It used to have three --
 * a join poll, a bounded burst of boot attempts, and an on-demand query on every
 * cache miss -- and all three were there because the state only arrived when
 * asked for.  What is left is one query that fires only when the ~1 Hz feed has
 * stopped for several seconds, which on a healthy link is never.
 *
 * WHAT A FALSE / -EAGAIN MEANS NOW.  Not "the answer is missing", but one of:
 * the AP is down or still starting, or the feed has stopped.  The remedy is the
 * same in every case and it is not this module's to apply -- retry, and the next
 * beat either answers or does not.  A retry a second later is likely to succeed,
 * which was not true when the answer needed a round trip to fetch.
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
 *  WHEN THE RADIO GOES UP AND DOWN -- ONE SWITCH, TWO ARRANGEMENTS
 *
 *  DEFAULT (0), and what ships: the AP's lifetime is the BLE CONNECTION's.  It is
 *  armed by ebadge_port_softap_arm() from the connect hook and stopped by
 *  ebadge_port_softap_shutdown() from the disconnect hook, and nothing in between
 *  moves it.  A session claims and releases it (start()/stop()) without ever
 *  switching it on or off.
 *
 *  Simple, and it has one cost worth naming: a phone that connects and sends
 *  nothing leaves a beacon on air for as long as it stays connected.
 *
 *  AT 1, the arrangement is per-TRANSFER instead: each offer arms the radio and
 *  each finished transfer schedules it down again (shutdown_when_idle()), so an
 *  idle connection carries no hotspot.  That is strictly more machinery -- see the
 *  two functions it gates -- and it is OFF because it is not yet validated on
 *  hardware.
 *
 *  THE TWO HALVES ARE ONE SWITCH ON PURPOSE.  Enabling the teardown without the
 *  per-offer arm reinstates a fixed bug: since protocol v3.1 sec.12.2 the 8711
 *  does not self-start its AP, so a radio switched off mid-connection stays off,
 *  and every transfer after the first fails forever with AP_START.  Splitting this
 *  into two macros would make that state expressible, so it is one.
 *----------------------------------------------------------------------------*/
#define EBADGE_SOFTAP_PER_TRANSFER_LIFETIME   0

/*----------------------------------------------------------------------------*
 *  Lifecycle
 *----------------------------------------------------------------------------*/

/**
 * @brief  Register the tick sink.  Call once from ebadge_task_init().
 *
 * Costs nothing on a healthy link: the tick reads the Wi-Fi state the 8711 pushes
 * and only queries when that feed has stopped.  Safe to call before the radio is
 * up -- the first tick's query is in fact what starts the SPI transport, and with
 * it the feed (see wifi_8711_at.h on why there is no init to do that).
 */
void ebadge_port_softap_init(void);

/**
 * @brief  Bring the AP up.  Call when a phone connects over BLE.
 *
 * Sends AT+WLSTARTAP, which since protocol v3.1 sec.12.2 is the ONLY way the AP
 * ever comes up -- the 8711 no longer self-starts it, so a device that never
 * calls this has no hotspot at all.  It is one command, not a sequence: the ports
 * used to need a follow-up AT+WLSTATE and now arrive on the next beat by
 * themselves.
 *
 * WHY A BLE CONNECTION IS THE RIGHT TRIGGER: a bring-up takes seconds to tens of
 * seconds, and an offer can arrive within one second of connecting.  Starting the
 * radio when the phone appears is what makes the offer answerable; starting it
 * when the offer arrives would guarantee a NOT_READY first.
 *
 * In the default arrangement this is the ONLY thing that raises the AP, and
 * ebadge_port_softap_shutdown() on the disconnect is the only thing that lowers
 * it -- see EBADGE_SOFTAP_PER_TRANSFER_LIFETIME for the alternative and why it is
 * off.
 *
 * Non-blocking, and bounded: retries are driven from the tick, and a state of
 * STARTING is waited out rather than re-requested (the 8711 is already retrying
 * internally every 5 s).  A phone that disconnects mid-sequence cancels it,
 * because the same disconnect switches the AP off.  Safe to call on every
 * connection -- a re-arm is a no-op when the AP is already up or a bring-up is
 * already in flight, which is what stops a reconnect from resetting the attempt
 * budget that bounds the wait.
 *
 * IT ALSO YIELDS TO WI-FI TRAFFIC.  Neither this nor any retry of it puts a
 * WLSTARTAP on the wire while JPGS or EBFS slots are arriving.  Such a slot is a
 * phone having associated with this AP and opened a TCP connection through it,
 * which proves the radio is up more directly than the state block describing it
 * ever can -- and asking to start a radio that is carrying a transfer would occupy
 * the single-flight AT link that transfer shares for a ~10 s round trip.  So when
 * the description and the traffic disagree, the traffic decides.
 *
 * CONTEXT: any thread.  It marshals itself onto l2_task.
 */
void ebadge_port_softap_arm(void);

#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
/**
 * @brief  Same request as ebadge_port_softap_arm(), for a caller already on l2_task.
 *
 * For the offer handlers.  ebadge_port_softap_arm() posts itself onto l2_task,
 * which for a caller that is ALREADY there means the arm lands after the current
 * handler returns -- i.e. after the ebadge_port_softap_start() the offer is about
 * to attempt has already run and refused.  The arm would still be useful, because
 * what it buys is the App's next retry rather than this attempt, but the ordering
 * would be an accident rather than a decision.  This entry point removes the hop.
 *
 * WHERE TO CALL IT: immediately before ebadge_port_softap_start(), NOT on arrival
 * of the offer.  An offer still in its validation gauntlet may yet be rejected for
 * being too large, malformed, or unstorable, and a radio raised for a transfer that
 * never runs has nothing to schedule it back down -- it would then stay up until
 * the BLE disconnect, which is the cost this whole arrangement exists to avoid.
 *
 * A RE-ARM IS NOT A RESTART.  This is a no-op when the AP is already up and
 * usable, and also when a bring-up is merely in flight or the AP reports STARTING.
 * That second case is load-bearing: an App handed NOT_READY retries about once a
 * second, and restarting the sequence would reset the attempt budget that bounds
 * it, letting a slow bring-up hold the sequence open indefinitely.
 *
 * Cancels a pending idle-down, for the same reason start() does: something wants
 * the radio.
 *
 * @param  why  short reason, logged.  The callers ask for the same sequence for
 *              different reasons and a log line that named only one of them
 *              misattributes the others.
 *
 * CONTEXT: l2_task.
 */
void ebadge_port_softap_arm_on_l2(const char *why);
#endif /* EBADGE_SOFTAP_PER_TRANSFER_LIFETIME */

/**
 * @brief  Claim the AP for a session and report what it actually is.
 *
 * Non-blocking; answers from the pushed Wi-Fi state.  Does NOT wait for a
 * bring-up: one takes seconds to tens of seconds and could not finish inside this
 * call, so a down AP is reported as -EAGAIN rather than waited out.
 *
 * It does not switch the radio on either.  In the default arrangement it does not
 * have to -- the AP was armed when the phone connected and stays up for the whole
 * connection, so "down" here means still starting, or a fault.  Under
 * EBADGE_SOFTAP_PER_TRANSFER_LIFETIME it re-arms on the -EAGAIN path instead,
 * because there "down" is a routine mid-connection state.
 *
 * @param  out_info   filled with the live SSID / password / channel / IP
 * @param  role       which of the 8711's two servers this session will use
 * @param  out_port   filled with the port for @p role
 * @param  joined_cb  invoked on l2_task when a STA associates; may be NULL
 *
 * @retval 0        AP up and usable; @p out_info and @p out_port are valid
 * @retval -EAGAIN  the AP is down, still starting, or the state feed has stopped.
 *                  Map to NOT_READY and let the App retry -- a retry a beat later
 *                  is likely to succeed.
 */
int  ebadge_port_softap_start(ebadge_softap_info_t *out_info,
                              ebadge_ap_port_role_t role,
                              uint16_t *out_port,
                              ebadge_softap_sta_joined_cb_t joined_cb);

/**
 * @brief  Stop caring about the AP.
 *
 * Does NOT take the radio down -- ebadge_port_softap_shutdown() is what does that,
 * from the BLE disconnect.  It drops the joined-callback, which is what stops this
 * module looking for an associated station.  Safe to call idempotently on cleanup
 * paths.
 *
 * Kept separate from the shutdown because releasing a claim and switching off a
 * radio are different decisions, and the preview stream makes that concrete: it
 * claims and releases the SAME AP, so a release must not be able to pull the radio
 * out from under whichever session claims it next.
 */
int  ebadge_port_softap_stop(void);

#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
/**
 * @brief  Switch the AP off once nothing is using it, after a settling delay.
 *
 * For the end of a file transfer, once the BLE verdict has gone out.  This is what
 * a per-transfer radio lifetime asks for -- the hotspot should not outlive the
 * transfer it was raised for -- without the two failure modes that made an
 * immediate teardown wrong when it was first tried:
 *
 *  1. THE VERDICT IS STILL IN FLIGHT.  [+XFERACK]:OK only means the 8711 accepted
 *     the command (spec sec.10.3); it then generates the EBXR and closes the TCP
 *     connection itself.  Cutting the radio at that instant can destroy the
 *     data-plane result the App may be reading.  Hence the delay: the schedule is
 *     armed here and the tick fires it SOFTAP_IDLE_DOWN_MS later.
 *
 *  2. THE NEXT TRANSFER HAD NO WAY BACK.  Since protocol v3.1 sec.12.2 the 8711
 *     does not self-start its AP, so a radio switched off mid-connection stays off
 *     unless something asks again -- which is why this is gated together with the
 *     per-offer arm rather than separately from it.
 *
 * Cancelled by anything that claims the AP again (ebadge_port_softap_start()) and
 * by the arm.  A pending schedule is also skipped if a session is live when it
 * fires, so a preview stream that starts inside the window is not cut off by the
 * file transfer that scheduled it.
 *
 * Idempotent: re-arming an already-pending schedule just restarts the delay.
 *
 * CONTEXT: l2_task.
 */
void ebadge_port_softap_shutdown_when_idle(void);
#endif /* EBADGE_SOFTAP_PER_TRANSFER_LIFETIME */

/**
 * @brief  Actually switch the AP off (AT+WLSTOPAP), now.
 *
 * Call on a BLE disconnect -- ebadge_port_ble.c's GAP hook -- which is the point
 * at which nothing can still want the radio: the phone is gone, so there is no
 * association to break, no TCP connection to cut and no 0x15/0x16 in flight to
 * strand.  In the default arrangement this is the ONLY thing that lowers the AP,
 * paired with the arm on the connect hook.
 *
 * WHY THIS IS WORTH DOING AT ALL: an idle SoftAP is a beacon on air and a radio
 * drawing current, so its lifetime is tied to something -- the BLE connection
 * here, or a single transfer under EBADGE_SOFTAP_PER_TRANSFER_LIFETIME.
 *
 * Non-blocking and fire-and-forget: there is no completion callback, because
 * there is no decision left to make on the answer.  A refusal means the AP was
 * already down or a start is racing us (spec sec.10.3, indistinguishable), and
 * neither warrants a retry -- the next arm re-raises the AP unconditionally, which
 * is what makes this safe to get wrong.
 *
 * Safe to call with no session and no AP: a stop against a down AP is answered
 * [+WLSTOPAP]:ERROR and costs one AT round trip.  That matters because the caller
 * does not know whether a transfer ever happened on the connection it is closing.
 *
 * Repeated calls collapse: a stop already in flight is not submitted twice, so a
 * teardown path that runs more than once costs one AT round trip, not several on a
 * single-flight link.
 *
 * CONTEXT: l2_task.
 */
void ebadge_port_softap_shutdown(void);

/** True between start() and stop() -- i.e. a session is relying on the AP.
 *  NOT "the radio is on": that is the AP= line of the Wi-Fi state, which
 *  ebadge_port_softap_info() is the way to consult.                            */
bool ebadge_port_softap_running(void);

/**
 * @brief  Describe the AP without claiming it for a session.
 *
 * For the 0x12 GET_AP_INFO handler, which must answer a phone that lost its
 * 0x13 without implying a transfer is in progress.
 *
 * Answers from the pushed Wi-Fi state and puts nothing on the wire, so it is
 * cheap enough to call per request.  True means the 8711 said AP=UP within the
 * last few seconds AND named the credentials -- i.e. the phone can act on this
 * now, not merely that the link has spoken at some point in the past.
 *
 * @param  role      which port to report -- see ebadge_ap_port_role_t
 * @param  out_port  the port for @p role; 0 if it is missing from the state
 *
 * @return true if the AP is up and describable; false leaves the outputs
 *         untouched and means down, starting, or a stopped state feed
 */
bool ebadge_port_softap_info(ebadge_softap_info_t *out_info,
                             ebadge_ap_port_role_t role,
                             uint16_t *out_port);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_SOFTAP_H_ */
