/**
 * @file    ebadge_port_antenna.h
 * @brief   The single shared antenna, and which radio currently owns it.
 *
 * ONE ANTENNA, TWO RADIOS.  P2_6 is a hardware RF switch, not an enable line:
 *
 *      P2_6 HIGH  ->  antenna wired to the 8773G's BLE radio
 *      P2_6 LOW   ->  antenna wired to the 8711's Wi-Fi radio
 *
 * There is no middle position and no sharing.  Whichever way the pin points, the
 * OTHER radio is off the air -- it still runs, still ACKs on its own bus, still
 * reports healthy, and simply cannot reach anything.  That is the property to keep
 * in mind while reading the rest of this header, because it makes every failure
 * here look like a peer fault rather than a local one.
 *
 * WHY BLE IS THE DEFAULT AND THE RESTING POSITION.  BLE is the control plane: it
 * carries the offer, the decision, AP_INFO, and the final verdict, and it is the
 * plane the App is required to believe (spec sec.5.3).  Wi-Fi carries only bulk
 * payload, and only inside a session that BLE negotiated. So Wi-Fi BORROWS the
 * antenna for a bounded window and BLE gets it back; the reverse arrangement would
 * mean a Wi-Fi fault could cost us the only channel able to report it.
 *
 * WHAT "BOUNDED WINDOW" MEANS EXACTLY, and it is narrower than "the session":
 *
 *   claim   after AP_INFO (0x13) has been handed to the BLE stack -- the phone
 *           cannot join a hotspot whose credentials never reached it
 *   release before the verdict (0x14 / 0x15 / 0x16) is emitted -- a notify sent
 *           while the antenna points at Wi-Fi goes nowhere
 *
 * Both edges are therefore inside the session, not around it.  Handing the antenna
 * over one call too early or one call too late produces the same symptom -- the
 * phone never hears from us -- and produces it on a stack where every layer logs
 * success, which is why the two rules are stated here rather than left to the call
 * sites to remember.
 *
 * REFERENCE-COUNTED, not a bare setter.  The two sessions (xfer_session and
 * stream_session) are mutually exclusive today, so a plain boolean would work --
 * but "mutually exclusive" is enforced by a check at the top of two offer
 * functions, and a count survives that check being weakened.  It also makes the
 * teardown paths idempotent, which matters because they are reached from four
 * directions and some of them run twice.
 *
 * Ownership is l2_task's.  Every caller is already on it (both sessions run
 * there), so there is no locking -- see the note in ebadge_port_antenna.c on why
 * this must not be called from the transport thread.
 */
#ifndef _EBADGE_PORT_ANTENNA_H_
#define _EBADGE_PORT_ANTENNA_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Which radio the antenna is wired to.  Named after the OWNER, not the pin
 *  level, so a call site reads as intent and a future board that inverts the
 *  switch changes one line in the .c rather than every caller. */
typedef enum
{
    EBADGE_ANT_OWNER_BT   = 0,   /**< P2_6 HIGH -- the resting position   */
    EBADGE_ANT_OWNER_WIFI = 1,   /**< P2_6 LOW  -- borrowed for a session */
} ebadge_ant_owner_t;

/**
 * @brief  Configure P2_6 and park the antenna on BLE.  Call once, early.
 *
 * Must run before the BLE stack starts advertising, because the pad's reset state
 * is not this: an unconfigured P2_6 leaves the switch wherever the last boot or a
 * pull left it, and a badge that advertises with the antenna pointed at the 8711
 * is simply invisible with no error anywhere.
 *
 * Idempotent -- a second call re-asserts BT and resets the count to zero.
 */
void ebadge_port_antenna_init(void);

/**
 * @brief  Hand the antenna to Wi-Fi for the duration of a data transfer.
 *
 * Call AFTER the BLE notify carrying AP_INFO (0x13) has been submitted, and never
 * before: that notify is what tells the phone which hotspot to join, and it cannot
 * be delivered once this returns.
 *
 * Nests.  The first claim moves the switch; later claims only count.
 */
void ebadge_port_antenna_claim_wifi(const char *why);

/**
 * @brief  Give the antenna back to BLE.
 *
 * Call BEFORE emitting the session verdict (0x14 / 0x15 / 0x16) and before any
 * other notify, and call it on EVERY exit path including the failures -- a session
 * that ends without releasing leaves the badge unreachable over BLE until the next
 * one happens to release, which for a device whose only control plane is BLE means
 * "until reboot".
 *
 * Balanced against claim().  A release with no outstanding claim is NORMAL, not an
 * error, and is silent: both sessions release from the same teardown choke point,
 * and that point is also on the early-reject paths which never claimed.  What does
 * warn is the genuine imbalance -- owner still Wi-Fi with the count at zero -- and
 * that case re-asserts BT rather than leaving the antenna stranded.
 */
void ebadge_port_antenna_release_wifi(const char *why);

/**
 * @brief  Force the antenna back to BLE regardless of the count.
 *
 * The recovery hatch, for the one case an unwind cannot express: a BLE disconnect
 * while a session is mid-flight tears down state from outside both sessions, and
 * the count may then never reach zero.  Also what a shell command would use.
 */
void ebadge_port_antenna_force_bt(const char *why);

/** Current owner -- for logs, shell output and asserts. */
ebadge_ant_owner_t ebadge_port_antenna_owner(void);

/** Outstanding Wi-Fi claims.  Nonzero with owner==BT is a bug. */
uint8_t ebadge_port_antenna_claims(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_ANTENNA_H_ */
