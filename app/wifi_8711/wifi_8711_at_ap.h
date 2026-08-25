/**
 * @file    wifi_8711_at_ap.h
 * @brief   The one global Wi-Fi state, and the SoftAP operations that change it.
 *
 * This is the layer that knows what the 8711's Wi-Fi state block MEANS.  It owns
 * the single copy of that state for the whole firmware, keeps it fed, and exposes
 * the two operations the protocol stack needs: "make sure the AP is up" and "tell
 * me its credentials".
 *
 * ---------------------------------------------------------------------------
 * THE STATE IS PUSHED TO US, ~1 Hz, WHETHER WE ASK OR NOT
 * ---------------------------------------------------------------------------
 * Protocol v3.1 sec.7.4 defines ONE state block with THREE carriers:
 *
 *   - the POLL payload            every ~1 s, unconditionally, no terminator
 *   - an AT+WLSTATE response      the same block + "[+WLSTATE]:OK"/":ERROR"
 *   - an unsolicited notification the same block, pushed once when a requested
 *                                 start succeeds
 *
 * So there is one parser with three feeds, and POLL is the primary one.  That is
 * what makes a single global state the natural shape here rather than a cache
 * with an invalidation policy: the truth arrives on its own, and everything else
 * on this surface is either a way to make the 8711 change that truth or a way to
 * ask for it a beat sooner.
 *
 * The consequences worth stating outright:
 *
 *   - NOTHING here polls.  There is no periodic WLSTATE.  A caller that wants
 *     the state reads it; a caller that finds it stale (the beat stopped) asks
 *     once.  That policy lives in ebadge_port_softap.c, which owns the tick.
 *   - `clients` is now fresh, not a snapshot from whenever someone last asked.
 *     Deciding "the phone has associated" from it is legitimate provided the
 *     state itself is not stale -- see wifi_8711_at_ap_cache_age_ms().
 *   - a query is a shortcut, not the source.  Skipping it costs at most one
 *     beat.
 *
 * ---------------------------------------------------------------------------
 * WE DO NOT OWN THE ACCESS POINT, AND SINCE v3.1 IT DOES NOT START ITSELF
 * ---------------------------------------------------------------------------
 * The 8711 runs the radio and its own TCP servers.  Its AT firmware has three AP
 * commands:
 *
 *   AT+WLSTARTAP   request a start -- the ONLY way the AP ever comes up
 *   AT+WLSTATE     read the state block now instead of waiting for the beat
 *   AT+WLSTOPAP    take the AP down
 *
 * sec.12.2 removed the boot-time self-start.  A device that sends no AT commands
 * therefore never has a hotspot at all -- both sides wait for the other, which
 * is exactly what it looked like in the field.  Someone must send WLSTARTAP; on
 * this firmware that is ebadge_port_softap_arm(), on BLE connect.
 *
 * This side still cannot choose the SSID, the password, the channel or the port.
 * It can only read what they are and switch the radio on and off.  Any code that
 * invents credentials and expects the phone to find that network is wrong.
 *
 * ---------------------------------------------------------------------------
 * THE STATE BLOCK IS THE ONLY SOURCE FOR THE CREDENTIALS
 * ---------------------------------------------------------------------------
 * WLSTARTAP's reply carries no credentials at all since sec.12.8 -- it used to
 * echo SSID= and PASSWORD=, this side ignored both, and the vendor then removed
 * them for the same reason: the SSID is derived from the MAC at bring-up time, so
 * a copy emitted at REQUEST time can describe a value that does not exist yet.
 * Its reply is a LIFECYCLE verdict and updates only the AP state.
 *
 * The consequence for callers: a successful wifi_8711_at_ap_start() does NOT mean
 * the credentials are known, and does not even mean the AP is up -- see
 * WIFI_8711_AP_STARTING.
 */
#ifndef _WIFI_8711_AT_AP_H_
#define _WIFI_8711_AT_AP_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  The AP= line of the state block (sec.7.4).  Three exhaustive values.
 *
 * A tri-state and not a bool, because the middle value is the one that changes
 * what a caller should DO: a bring-up takes seconds to tens of seconds, the 8711
 * retries it internally every 5 s, and re-sending WLSTARTAP during that window
 * asks for something already in progress.  With a bool, STARTING is
 * indistinguishable from DOWN and the natural reaction to it is the wrong one.
 */
typedef enum
{
    WIFI_8711_AP_DOWN = 0,  /**< nobody has requested it; it will not self-start */
    WIFI_8711_AP_STARTING,  /**< request accepted, bring-up running -- just wait */
    WIFI_8711_AP_UP,        /**< credentials below are real and usable           */
} wifi_8711_ap_state_t;

/** The AP= spelling of @p state, for logs.  Never NULL. */
const char *wifi_8711_ap_state_str(wifi_8711_ap_state_t state);

/** Wi-Fi state as reported by the 8711 -- the global state's payload.
 *
 *  Field widths follow the 802.11 / protocol maxima rather than what the
 *  current firmware happens to emit, so a longer SSID in a future build cannot
 *  overflow anything here.
 *
 *  Everything below `state` is zero unless `state == WIFI_8711_AP_UP`: sec.7.4
 *  fixes the block's layout and expresses "no value" as a zero value rather than
 *  as a missing line, so a down AP reports SSID= with nothing after it. */
typedef struct
{
    wifi_8711_ap_state_t state;  /**< AP= -- read this before anything else     */

    char     ssid[33];      /**< NUL-terminated, up to 32 B                    */
    char     password[64];  /**< NUL-terminated; "" means an open network      */
    uint32_t ip;            /**< AP-side IPv4, HOST order (0xC0A82B01)         */

    /*------------------------------------------------------------------------*
     *  TWO PORTS, AND THEY ARE NOT INTERCHANGEABLE
     *
     *  The 8711 runs two TCP servers with different admission rules (see
     *  note/refer/phone-to-8711-jpeg-tcp-protocol.md and SPI spec sec.6):
     *
     *    stream_port (PORT=, 5004)      raw JPEG only -- FF D8 .. FF D9, at most
     *                                   61440 B per frame.  An EBXF header sent
     *                                   here is rejected: the door does not
     *                                   accept it.
     *    file_port   (FILE_PORT=, 9000) EBXF header + body, re-wrapped into EBFS
     *                                   slots, up to 2 MiB per file.
     *
     *  They were one field until a file transfer was pointed at 5004 and the
     *  phone silently got nowhere.  Keeping them separate is what makes that
     *  mix-up impossible to express: a file caller reads file_port, a preview
     *  caller reads stream_port, and neither can accidentally get the other.
     *------------------------------------------------------------------------*/
    uint16_t stream_port;   /**< PORT= -- raw-JPEG preview (5004 in practice)  */
    uint16_t file_port;     /**< FILE_PORT= -- EBXF upload (9000 in practice)  */

    uint8_t  channel;       /**< 2.4 GHz channel, or 0 if the AP is not up     */

    /** CLIENTS= -- associated STAs as of the last block we received.
     *
     *  Fresh to within one beat while the link is alive, which is what makes it
     *  usable as the "phone has associated" edge.  Check the state's age before
     *  trusting it: on a link that has gone quiet this is simply the last thing
     *  the 8711 said, and acting on it would start a transfer against a phone
     *  that may have left.
     *
     *  Note a STA counts here from ASSOCIATION, before DHCP -- such a client
     *  appears in the CLIENT= list with IP=0.0.0.0, which is a lease that has
     *  not arrived, not a failure. */
    uint8_t  clients;
} wifi_8711_ap_info_t;

/**
 * @brief  Completion of an AP query or start.
 *
 * @param  ok    true if the reply was usable.  For a query that means @p info is
 *               populated from it; for a start it means the request was accepted
 *               and @p info carries the resulting state.
 * @param  info  never NULL, but all-zero when nothing is known yet -- so a
 *               caller that needs a field must check it, not assume it
 * @param  user  the pointer handed to the request
 *
 * CONTEXT: transport thread or system workqueue -- NOT the protocol stack's
 * l2_task.  Anything touching protocol state must marshal itself over with
 * ebadge_task_post_call().
 */
typedef void (*wifi_8711_ap_cb_t)(bool ok, const wifi_8711_ap_info_t *info,
                                  void *user);

/**
 * @brief  Completion of an AP stop.
 *
 * Separate from wifi_8711_ap_cb_t and carrying no info struct, because
 * "[+WLSTOPAP]:OK" has no fields to report -- handing over an all-zero
 * wifi_8711_ap_info_t would invite a caller to read an SSID out of a successful
 * stop.
 *
 * @param  ok    true only on an explicit "[+WLSTOPAP]:OK".  See
 *               wifi_8711_at_ap_stop() for why false is not "still up".
 *
 * CONTEXT: transport thread or system workqueue -- NOT l2_task.
 */
typedef void (*wifi_8711_ap_stop_cb_t)(bool ok, void *user);

/*----------------------------------------------------------------------------*
 *  The global state
 *----------------------------------------------------------------------------*/

/**
 * @brief  Feed the global state from a raw sec.7.4 block.
 *
 * The single entry point for every carrier of that block, which is what keeps
 * one copy of the truth: the AT layer calls this for a POLL payload and for any
 * unsolicited notification, and the WLSTATE reply path calls it too.
 *
 * @param  text  NUL-terminated block, with or without a terminator line
 * @return true if @p text really was a state block and the state was updated;
 *         false if it was something else entirely, which is how the caller tells
 *         an unsolicited notification from a reply that simply arrived too late
 *
 * CONTEXT: any thread.  Takes the state lock; runs no callbacks, so it is safe
 * from the transport thread's slot sink.
 */
bool wifi_8711_at_ap_ingest(const char *text);

/**
 * @brief  Read the global Wi-Fi state without going on the wire.
 *
 * For synchronous callers such as BLE command handlers, which cannot wait
 * seconds for a reply.  This is the intended way to answer them: the state is
 * pushed to us ~1 Hz, so reading it is not a shortcut around asking the 8711 --
 * it IS what the 8711 last said.
 *
 * @param  out  filled in only when this returns true
 * @return true if a state block has ever arrived; false if the link has never
 *         said anything, which is a different answer from "the AP is down"
 *
 * Pair it with wifi_8711_at_ap_cache_age_ms() when the answer must be current:
 * this returns the last known state however old, and on a link whose beat has
 * stopped that is a description of the past.
 */
bool wifi_8711_at_ap_cached(wifi_8711_ap_info_t *out);

/** Time since the global state was last fed, in ms, or UINT32_MAX if no block
 *  has ever arrived.
 *
 *  Expect this to sit under ~1 s on a healthy link.  Anything much larger means
 *  the POLL beat has stopped, which is a transport fault rather than an AP one --
 *  sec.10 is explicit that AT-link availability and AP state are independent. */
uint32_t wifi_8711_at_ap_cache_age_ms(void);

/*----------------------------------------------------------------------------*
 *  Operations
 *----------------------------------------------------------------------------*/

/**
 * @brief  Ask for the state block now ("AT+WLSTATE") instead of waiting.
 *
 * A SHORTCUT, not the source: the same block arrives on its own every beat, so
 * the only thing this buys is up to one beat of latency, and the only situation
 * that genuinely needs it is one where the beat has stopped and we want to know
 * whether the link answers at all.
 *
 * No precondition since sec.10.1: it answers whether or not the AP is running,
 * with "[+WLSTATE]:ERROR" appended when it is not.  The block is present either
 * way, so the global state is updated either way.
 *
 * Asynchronous: 0 means "queued", not "answered".
 *
 * @retval 0        queued
 * @retval -EBUSY   another AT command is outstanding (single flight)
 * @retval -ENODEV  no 8711 link on this build / init never succeeded
 * @retval <0       staging error
 */
int wifi_8711_at_ap_query(wifi_8711_ap_cb_t cb, void *user);

/**
 * @brief  Request a SoftAP start ("AT+WLSTARTAP").
 *
 * The only way the AP ever comes up (sec.12.2 removed the self-start), and its
 * reply is a LIFECYCLE verdict rather than a description -- it carries no
 * credentials at all since sec.12.8.
 *
 * `ok` means THE REQUEST WAS ACCEPTED.  It does NOT mean the AP is up: a real
 * bring-up takes seconds to tens of seconds, and the 8711 answers immediately so
 * as not to freeze the SPI control link while it runs.  Which of the two
 * happened is in the state handed to @p cb -- WIFI_8711_AP_UP means the call was
 * redundant, WIFI_8711_AP_STARTING means the bring-up is now running.
 *
 * Do not re-send while the state is STARTING.  It is safe (the 8711 tolerates
 * it) but it asks for something already in progress; the POLL beat is what
 * reports the outcome.
 *
 * Same asynchronous contract as the query.
 */
int wifi_8711_at_ap_start(wifi_8711_ap_cb_t cb, void *user);

/**
 * @brief  Take the AP down ("AT+WLSTOPAP").
 *
 * The one operation on this surface that switches the radio off, so unlike
 * start() it is not idempotent, and its outcome is a verdict rather than a
 * description -- hence a different callback type, with no info struct: the reply
 * is "[+WLSTOPAP]:OK" or "[+WLSTOPAP]:ERROR" and carries no fields at all.
 *
 * WHAT `ok == false` DOES AND DOES NOT MEAN.  The 8711 answers ERROR both when
 * the AP was already down and when a start is still in progress (sec.10.3), and
 * the reply does not say which.  So a false here is NOT evidence that the AP is
 * still up, and a caller must not retry on the strength of it: the already-down
 * case would retry forever, and the start-in-progress case is a race this side
 * cannot win by asking again sooner.  Treat it as "the AP's state is now
 * unknown" and let the next beat establish the truth -- which now takes about a
 * second rather than a round trip.
 *
 * The credentials are deliberately left ALONE, successful or not.  SSID,
 * password and ports survive an AP cycle -- the 8711 has no way to change
 * them -- and the next block overwrites them anyway.
 *
 * Same asynchronous contract as the queries.
 */
int wifi_8711_at_ap_stop(wifi_8711_ap_stop_cb_t cb, void *user);

/*----------------------------------------------------------------------------*
 *  Parser -- exposed for unit-testing against captured blocks
 *----------------------------------------------------------------------------*/

/**
 * @brief  Parse a sec.7.4 state block into @p out.
 *
 * Accepts all three carriers: a bare POLL payload, an AT+WLSTATE reply with its
 * terminator, and an unsolicited notification.  The layout is fixed, so this
 * does not branch on the AP state before reading the keys -- a down AP still
 * emits every line, with zero values.
 *
 * A WLSTARTAP body is REJECTED rather than parsed, so the rule that credentials
 * come only from the state block holds even when this is called with the wrong
 * body.
 *
 * @return true if the block was recognisable as Wi-Fi state.  Note this is
 *         independent of whether the AP is up: an "AP=DOWN" block is a
 *         successful parse of informative content, and reporting it as a failure
 *         is what made a down AP indistinguishable from a broken link.
 */
bool wifi_8711_at_ap_parse(const char *text, wifi_8711_ap_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_8711_AT_AP_H_ */
