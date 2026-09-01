/**
 * @file    ebadge_port_softap.h
 * @brief   SoftAP control for the Wi-Fi data plane.
 *
 * ---------------------------------------------------------------------------
 * WE CONFIGURE THE AP NOW.  THE READ-BACK IS JUST HOW CALLERS ASK WHAT WE CHOSE
 * ---------------------------------------------------------------------------
 * The backing radio is the RTL8711FA reached over SPI, running Realtek's standard
 * AT command set.  There the SSID, password and channel are ARGUMENTS to
 * AT+WLSTARTAP and the TCP listeners are ours to create with AT+SKTSERVER -- so
 * unlike the in-house firmware this replaced, nothing about the AP is discovered.
 * This module picks the credentials, remembers them, and asks the 8711 to be that.
 *
 * ebadge_port_softap_start() nevertheless still hands the credentials BACK rather
 * than taking them, and that shape is worth keeping for a reason that did not
 * change: the two session state machines used to invent their own ("eBadge-XFR" /
 * 192.168.4.1:9000) and send them to the phone in 0x13 AP_INFO, which sent it
 * looking for a network nobody had configured.  One module choosing and everybody
 * else reading is what makes that unrepresentable.
 *
 * ---------------------------------------------------------------------------
 * NOTHING BLOCKS, NOTHING POLLS, AND NOW NOTHING GOES STALE EITHER
 * ---------------------------------------------------------------------------
 * Every reader here answers from at_wifi's copy of what we asked for, in
 * microseconds, on l2_task -- which matters because l2_task is the single
 * serialiser for the whole protocol stack and an AT round trip is seconds.
 *
 * This module used to guard every one of those reads with a freshness rule,
 * because the answers arrived in a ~1 Hz state block the 8711 pushed and a block
 * from ten seconds ago could describe a radio that had since been switched off.
 * There is no feed now, so there is nothing to go stale: `up` is set when OUR
 * AT+WLSTARTAP answered OK and cleared when our stop completed or the 8711
 * announced a reboot.  It is a record of what we did, not a report we received,
 * and the whole staleness apparatus went with the feed.
 *
 * WHAT A FALSE / -EAGAIN MEANS NOW.  The AP is down, or a bring-up is still
 * running.  Worth retrying: a bring-up takes up to ~25 s (AT+WLSTARTAP does not
 * answer until the radio is genuinely on air), so an offer refused during one is
 * answerable once it lands.
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
 *  ONE PORT, AND WHY THE ROLE PARAMETER IS GONE WITH THE SECOND ONE
 *
 *  There used to be two listeners -- 9000 for an EBXF file upload, 5004 for bare
 *  JPEG preview frames -- and callers named which they wanted, because handing a
 *  file to the preview door got it scanned for JPEG markers and timed out with
 *  nothing pointing at the cause.
 *
 *  Both sessions now use 9000, and what makes that safe is that they are MUTUALLY
 *  EXCLUSIVE: each *_offer() rejects with BUSY while the other is non-IDLE
 *  (stream_session.c and xfer_session.c both check, in both directions), because
 *  both want this one radio.  So the two byte formats can never be in flight at
 *  the same time, and the port was never what chose the parser anyway -- the ARMED
 *  SESSION is.  ebadge_port_tcp.c forwards bytes verbatim to whichever session
 *  called listen(), so a 40-byte EBXF header and a bare JPEG are told apart by who
 *  is listening, not by which door they came through.
 *
 *  A role enum with one member would suggest a choice that no longer exists, so
 *  the parameter is removed rather than defaulted.  Restoring the second door
 *  means restoring both -- the constant and the caller's say in it -- together.
 *----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
 *  THE PORT IS OURS TO PICK NOW, SO IT IS NAMED HERE
 *
 *  It used to be discovered: the in-house firmware ran the servers itself and
 *  reported FILE_PORT= / PORT= in its state block.  With AT+SKTSERVER the listener
 *  is created by ebadge_port_tcp.c from this constant, so the number has to live
 *  somewhere, and one declaration is what stops the door we open and the port we
 *  advertise in 0x13 drifting apart.
 *
 *  The value is unchanged from the old firmware on purpose: the phone App has it
 *  compiled in.
 *----------------------------------------------------------------------------*/
#define EBADGE_AP_FILE_PORT      9000U

/** The 8711's own SoftAP address, and thus ours: atcmd_wifi.c documents
 *  192.168.43.1 as the default for AT+WLSTARTAP's `ip`/`gw` keys, and we do not
 *  override them.  Reported in 0x13 AP_INFO so the App can reach the ports above
 *  without inferring the gateway from its own DHCP lease. */
#define EBADGE_AP_IPV4           0xC0A82B01U   /* 192.168.43.1 */

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
 *  per-offer arm reinstates a fixed bug: nothing else raises the AP, so a radio
 *  switched off mid-connection stays off, and every transfer after the first fails
 *  forever with AP_START.  Splitting this into two macros would make that state
 *  expressible, so it is one.
 *----------------------------------------------------------------------------*/
#define EBADGE_SOFTAP_PER_TRANSFER_LIFETIME   0

/*----------------------------------------------------------------------------*
 *  Lifecycle
 *----------------------------------------------------------------------------*/

/**
 * @brief  Register the tick sink and bring up the AT layer.  Call once from
 *         ebadge_task_init().
 *
 * Puts nothing on the wire: the radio is raised by ebadge_port_softap_arm() when
 * a phone connects, and until then the AT link sits idle.  Safe to call before the
 * 8711 has finished booting -- a command submitted before its "ATCMD READY" is
 * queued rather than refused (see at_client.h), so the startup order of the two
 * chips is not load-bearing.
 */
void ebadge_port_softap_init(void);

/**
 * @brief  Bring the AP up.  Call when a phone connects over BLE.
 *
 * Sends AT+WLSTARTAP with the credentials this module derives from the BLE
 * address, and it is the only thing that ever raises the radio -- a device that
 * never calls this has no hotspot at all.
 *
 * ONE COMMAND, AND ITS "OK" MEANS THE RADIO IS ON AIR.  That is not the usual AT
 * contract, and it is what removed most of this module: the 8711's handler calls
 * wifi_start_ap() and then polls its own driver once a second for up to 20
 * iterations, comparing the running SSID against the one asked for, and only then
 * starts the DHCP server and prints OK.  So there is no STARTING state to model,
 * no follow-up query to fetch the ports (we chose them), and no ladder of attempts
 * to wait out a bring-up -- the waiting happens inside the command.
 *
 * WHY A BLE CONNECTION IS THE RIGHT TRIGGER: that bring-up is up to ~25 s, and an
 * offer can arrive within a second of connecting.  Starting the radio when the
 * phone appears is what makes the offer answerable; starting it when the offer
 * arrives would guarantee a NOT_READY first.
 *
 * In the default arrangement this is the ONLY thing that raises the AP, and
 * ebadge_port_softap_shutdown() on the disconnect is the only thing that lowers
 * it -- see EBADGE_SOFTAP_PER_TRANSFER_LIFETIME for the alternative and why it is
 * off.
 *
 * Non-blocking.  Safe to call on every connection: a re-arm is a no-op when the AP
 * is already up or a bring-up is already in flight, which is what stops a
 * reconnect from restarting a 25 s command that is already running.
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
 * A RE-ARM IS NOT A RESTART.  This is a no-op when the AP is already up, and also
 * when a bring-up is merely in flight.  That second case is load-bearing: an App
 * handed NOT_READY retries about once a second, and a WLSTARTAP occupies the
 * single-flight AT layer for up to ~25 s, so an arm per retry would queue a dozen
 * of them behind the one already doing the work.
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
 * Non-blocking; answers from what we asked the 8711 to be.  Does NOT wait for a
 * bring-up: one takes up to ~25 s and could not finish inside this call, so a down
 * AP is reported as -EAGAIN rather than waited out.
 *
 * It does not switch the radio on either.  In the default arrangement it does not
 * have to -- the AP was armed when the phone connected and stays up for the whole
 * connection, so "down" here means a bring-up still running, or a fault.  Under
 * EBADGE_SOFTAP_PER_TRANSFER_LIFETIME it re-arms on the -EAGAIN path instead,
 * because there "down" is a routine mid-connection state.
 *
 * @param  out_info   filled with the SSID / password / channel / IP in use
 * @param  out_port   filled with the single TCP port both sessions use (9000)
 * @param  joined_cb  invoked on l2_task when a STA associates; may be NULL
 *
 * @retval 0        AP up and usable; @p out_info and @p out_port are valid
 * @retval -EAGAIN  the AP is down or still coming up.  Map to NOT_READY and let
 *                  the App retry.
 */
int  ebadge_port_softap_start(ebadge_softap_info_t *out_info,
                              uint16_t *out_port,
                              ebadge_softap_sta_joined_cb_t joined_cb);

/**
 * @brief  Stop caring about the AP.
 *
 * Does NOT take the radio down -- ebadge_port_softap_shutdown() is what does that,
 * from the BLE disconnect.  It drops the joined-callback, which is what stops this
 * module reporting an association to a session that has finished with it.  Safe to
 * call idempotently on cleanup paths.
 *
 * Kept separate from the shutdown because releasing a claim and switching off a
 * radio are different decisions, and the preview stream makes that concrete: it
 * claims and releases the SAME AP, so a release must not be able to pull the radio
 * out from under whichever session claims it next.
 */
int  ebadge_port_softap_stop(void);

/**
 * @brief  A phone opened a TCP connection, which is this stack's "a station
 *         joined" edge.  Called by ebadge_port_tcp.c.
 *
 * NOT part of the surface a session uses -- sessions receive this through the
 * joined_cb they handed to ebadge_port_softap_start(), and this is how that
 * callback gets fired.  It is declared here rather than in ebadge_port_tcp.h
 * because the promise to deliver the edge is one this module made.
 *
 * WHY THE EDGE COMES FROM THE SOCKET LAYER AT ALL.  The standard AT firmware emits
 * no association events: atcmd_wifi.c contains not one at_printf_indicate() call,
 * so there is nothing to subscribe to, and the only other way to notice an
 * association is to poll AT+WLSTATE for its "Client Num:" line -- a round trip per
 * beat on the same single-flight link the transfer needs.  The seed-connect
 * indication a socket server emits is event-driven instead, and it is closer to
 * what a session is actually waiting for (somebody able to send us bytes) than a
 * layer-2 association would be.
 *
 * Ignored unless a session is claiming the AP and has not been told already, so a
 * phone that opens a second connection does not deliver a second edge.
 *
 * CONTEXT: l2_task.
 */
void ebadge_port_softap_report_sta_joined(void);

#if EBADGE_SOFTAP_PER_TRANSFER_LIFETIME
/**
 * @brief  Switch the AP off once nothing is using it, after a settling delay.
 *
 * For the end of a file transfer, once the BLE verdict has gone out.  This is what
 * a per-transfer radio lifetime asks for -- the hotspot should not outlive the
 * transfer it was raised for -- without the two failure modes that made an
 * immediate teardown wrong when it was first tried:
 *
 *  1. THE VERDICT IS STILL IN FLIGHT.  The EBXR ack is eight bytes we push
 *     ourselves over the phone's own TCP connection, and the 0x15/0x16 notify has
 *     only just been queued on the BLE link.  Cutting the radio at that instant
 *     destroys the data-plane result the App may be reading.  Hence the delay: the
 *     schedule is armed here and the tick fires it SOFTAP_IDLE_DOWN_MS later.
 *
 *  2. THE NEXT TRANSFER HAD NO WAY BACK.  Nothing else raises the AP, so a radio
 *     switched off mid-connection stays off unless something asks again -- which is
 *     why this is gated together with the per-offer arm rather than separately from
 *     it.
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
 * It also destroys BOTH TCP listeners, because stopping the radio destroys the
 * interface they are bound to: keeping the link ids would leave us routing a later
 * phone's bytes through servers the 8711 no longer has.  They are recreated on the
 * next arm rather than by the next session -- ebadge_port_tcp_open_servers() is
 * called from the AP-start completion, so the doors and the radio go up and down
 * as one action.
 *
 * WHY THIS IS WORTH DOING AT ALL: an idle SoftAP is a beacon on air and a radio
 * drawing current, so its lifetime is tied to something -- the BLE connection
 * here, or a single transfer under EBADGE_SOFTAP_PER_TRANSFER_LIFETIME.
 *
 * Non-blocking and fire-and-forget: there is no completion callback, because there
 * is no decision left to make on the answer.  AT+WLSTOPAP has no failure path in
 * the 8711's implementation -- it deinits DHCP, calls wifi_stop_ap() and prints OK
 * -- so an outcome other than OK means the AT link broke rather than that the radio
 * survived, and the AP is marked down either way.
 *
 * Safe to call with no session and no AP: it costs one AT round trip.  That matters
 * because the caller does not know whether a transfer ever happened on the
 * connection it is closing.
 *
 * Repeated calls collapse: a stop already in flight is not submitted twice, so a
 * teardown path that runs more than once costs one AT round trip, not several on a
 * single-flight link.
 *
 * CONTEXT: l2_task.
 */
void ebadge_port_softap_shutdown(void);

/** True between start() and stop() -- i.e. a session is relying on the AP.
 *  NOT "the radio is on": ebadge_port_softap_info() is the way to consult that. */
bool ebadge_port_softap_running(void);

/**
 * @brief  Describe the AP without claiming it for a session.
 *
 * For the 0x12 GET_AP_INFO handler, which must answer a phone that lost its
 * 0x13 without implying a transfer is in progress.
 *
 * Puts nothing on the wire, so it is cheap enough to call per request.  True means
 * an AT+WLSTARTAP of ours answered OK and no stop has completed since -- i.e. the
 * phone can act on this now.
 *
 * @param  out_port  the TCP port to advertise -- always EBADGE_AP_FILE_PORT
 *
 * @return true if the AP is up and describable; false leaves the outputs
 *         untouched and means down, or still coming up
 */
bool ebadge_port_softap_info(ebadge_softap_info_t *out_info,
                             uint16_t *out_port);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_SOFTAP_H_ */
