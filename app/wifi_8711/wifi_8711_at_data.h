/**
 * @file    wifi_8711_at_data.h
 * @brief   RESERVED bidirectional data tunnel over the ATMC channel.
 *
 * Two commands, one per direction, for the case where the BLE control plane and
 * the Wi-Fi side need to pass small messages that are not JPEG frames and not
 * AP credentials:
 *
 *   AT+WLRECV    pull whatever arrived over Wi-Fi        (8711 -> us -> BT)
 *   AT+WLSEND=   push a message out over Wi-Fi           (BT -> us -> 8711)
 *
 * ---------------------------------------------------------------------------
 * READ THIS FIRST: NEITHER COMMAND EXISTS IN THE 8711 FIRMWARE TODAY
 * ---------------------------------------------------------------------------
 * The AT firmware has exactly two commands, AT+WLSTATE and AT+WLSTARTAP, and
 * answers "[AT]:ERROR" to everything else (vendor spec sec.7.3, which also says
 * new commands must be agreed with them).  So every call in this file will fail
 * against current firmware, by design: this is a *reservation* that fixes the
 * names, the framing and the size budget now, so that the day the 8711 side
 * implements them nothing on this side has to be redesigned.
 *
 * The failure is handled rather than merely expected.  The first "[AT]:ERROR"
 * latches @ref wifi_8711_at_data_supported() to false, and every later call is
 * refused locally with -ENOTSUP without going on the wire.  Without that latch a
 * periodic poll would spend a 2..4 s AT transaction, every period, forever, to
 * be told "no" -- and it would do so while holding the single-flight slot that
 * port_softap needs for its AP queries.
 *
 * ---------------------------------------------------------------------------
 * WHY THE TWO DIRECTIONS ARE NOT SYMMETRIC
 * ---------------------------------------------------------------------------
 * The wire is not symmetric, so an API that pretended otherwise would be
 * lying:
 *
 *   - A COMMAND payload must stay under 128 B, because that is the size of the
 *     8711's parse buffer and anything longer is dropped *silently* (sec.9).
 *     After the "AT+WLSEND=" prefix, the CRLF and hex expansion, that leaves
 *     room for WIFI_8711_AT_DATA_SEND_MAX bytes -- tens, not hundreds.
 *   - A RESPONSE payload may be up to 4064 B, so the receive direction is far
 *     roomier.  Its real ceiling is not the slot but OUR OWN text buffer,
 *     WIFI_8711_AT_TEXT_MAX (512 B), which is where
 *     WIFI_8711_AT_DATA_RECV_MAX comes from.
 *
 * Both figures are derived with BUILD_ASSERTs in the .c file rather than
 * hand-written, so a change to either buffer cannot silently invalidate them.
 *
 * This is a CONTROL-PLANE tunnel.  Bulk data does not belong here: a JPEG frame
 * is ~60 KiB and would need a thousand round trips at 2..4 s each.  Frames have
 * their own path (JPGS slots, see wifi_xfer/jpgs_ingress.h).
 *
 * ---------------------------------------------------------------------------
 * WHY PAYLOADS ARE HEX
 * ---------------------------------------------------------------------------
 * The ATMC channel is a *text* channel end to end: the transaction layer copies
 * a reply into a char buffer and NUL-terminates it, so a binary byte of 0x00
 * would truncate the message, and a 0x0D/0x0A would end the AT line early --
 * the 8711 strips trailing CR/LF before matching.  Hex sidesteps all three, and
 * on a control-plane payload the 2x cost is worth paying for a format that is
 * also readable in a log capture.  If a future firmware offers a real binary
 * framing, that is a different transport and should be a different file.
 *
 * ---------------------------------------------------------------------------
 * RECEIVE IS A POLL, AND THAT IS NOT A CHOICE EITHER
 * ---------------------------------------------------------------------------
 * The 8711 cannot tell us anything we did not ask for: the AT channel only ever
 * answers a command, and B2W is a readiness gate rather than a request line
 * (sec.11.1 -- it is raised on every re-arm regardless of content, so it cannot
 * encode "I have data").  So inbound data has to be pulled.
 *
 * This file deliberately does NOT register a tick sink to poll on its own:
 *
 *   - the AT layer is single flight, so a background data poll would compete
 *     with port_softap's AP query and each would see the other's -EBUSY;
 *   - EBADGE_TICK_SINKS is 4 and three are already taken;
 *   - only the eventual owner of this tunnel knows when it is worth asking.
 *
 * So polling is the caller's decision.  wifi_8711_at_data_poll() asks once.
 */
#ifndef _WIFI_8711_AT_DATA_H_
#define _WIFI_8711_AT_DATA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  Reserved command strings
 *
 *  Deliberately NOT added to spi_at_protocol.h: that header is the vendor's
 *  copy, and putting unimplemented commands in it would make them look agreed.
 *  They follow the vendor's AT+WL* family so a future firmware can adopt them
 *  without a naming discussion, and they are kept short because every byte of
 *  the name is a byte taken off the send payload budget.
 *----------------------------------------------------------------------------*/

/** Pull data that arrived over Wi-Fi.  No argument. */
#define WIFI_8711_AT_CMD_WLRECV        "AT+WLRECV\r\n"

/** Push data out over Wi-Fi.  Followed by hex, then CRLF. */
#define WIFI_8711_AT_CMD_WLSEND_PREFIX "AT+WLSEND="

/** Largest outbound message, in bytes before hex expansion.
 *
 *  Derived from the 128 B peer parse buffer:
 *    127 usable - strlen("AT+WLSEND=") - strlen("\r\n") = 115 hex chars,
 *  i.e. 57 bytes.  Rounded down to 56 to keep the number even and leave a byte
 *  of slack.  A BUILD_ASSERT in the .c file proves the framed command still
 *  fits, so this cannot drift out of agreement with the prefix. */
#define WIFI_8711_AT_DATA_SEND_MAX     56U

/** Largest inbound message, in bytes after hex decoding.
 *
 *  Bounded by WIFI_8711_AT_TEXT_MAX (512 B) rather than by the 4064 B slot:
 *  the reply also carries its own "LEN=" / "DATA=" / "[+WLRECV]:OK" framing, so
 *  224 B (448 hex chars) leaves comfortable room for it.  Raising this means
 *  raising the AT text buffer first. */
#define WIFI_8711_AT_DATA_RECV_MAX     224U

/*----------------------------------------------------------------------------*
 *  Callbacks
 *
 *  CONTEXT for both: the transport thread or the system workqueue -- never the
 *  protocol stack's l2_task.  Anything that touches protocol state must marshal
 *  itself across with ebadge_task_post_call().  Both must also return promptly:
 *  on the success path B2W stays low for the whole callback, which
 *  back-pressures the 8711.
 *----------------------------------------------------------------------------*/

/**
 * @brief  A message arrived from the Wi-Fi side.
 *
 * @param  data  decoded bytes; valid only for the duration of the call
 * @param  len   1..WIFI_8711_AT_DATA_RECV_MAX -- never 0, an empty reply is
 *               reported as "nothing pending" instead of an empty message
 * @param  user  the pointer given to wifi_8711_at_data_set_rx_sink()
 */
typedef void (*wifi_8711_data_rx_cb_t)(const uint8_t *data, size_t len,
                                       void *user);

/**
 * @brief  Outcome of one poll or send.
 *
 * @param  ok    true if the 8711 accepted the command / answered usefully
 * @param  user  the pointer given to the request
 *
 * For a poll, ok=true with no preceding rx callback means "asked successfully,
 * the 8711 had nothing".  The two are worth telling apart: a caller that is
 * draining a queue needs to know whether to ask again immediately.
 */
typedef void (*wifi_8711_data_done_cb_t)(bool ok, void *user);

/*----------------------------------------------------------------------------*
 *  API
 *----------------------------------------------------------------------------*/

/**
 * @brief  Install the sink for inbound messages (NULL to discard them).
 *
 * Set this before the first poll, otherwise a message that does arrive is
 * decoded, logged and dropped.
 */
void wifi_8711_at_data_set_rx_sink(wifi_8711_data_rx_cb_t cb, void *user);

/**
 * @brief  Ask the 8711 once whether anything arrived over Wi-Fi.
 *
 * Asynchronous: 0 means "queued", not "answered".  If the 8711 has data, the rx
 * sink fires before @p cb.
 *
 * @retval 0         queued
 * @retval -ENOTSUP  the firmware has already told us it does not know this
 *                   command; not attempted again
 * @retval -EBUSY    another AT command is outstanding (single flight)
 * @retval -ENODEV   no 8711 link on this build / init never succeeded
 * @retval <0        staging error
 */
int wifi_8711_at_data_poll(wifi_8711_data_done_cb_t cb, void *user);

/**
 * @brief  Hand @p len bytes to the 8711 for transmission over Wi-Fi.
 *
 * @param  data  copied before returning, so a stack buffer is fine
 * @param  len   1..WIFI_8711_AT_DATA_SEND_MAX
 *
 * Same asynchronous contract as the poll.  Note that success means the 8711
 * accepted the message, not that a peer received it -- there is no end-to-end
 * acknowledgement in this framing, and adding one is part of what has to be
 * agreed with the vendor.
 *
 * @retval 0          queued
 * @retval -EMSGSIZE  len is 0 or above WIFI_8711_AT_DATA_SEND_MAX
 * @retval -ENOTSUP   firmware does not implement it (see the poll)
 * @retval -EBUSY / -ENODEV / <0   as wifi_8711_at_data_poll()
 */
int wifi_8711_at_data_send(const uint8_t *data, size_t len,
                           wifi_8711_data_done_cb_t cb, void *user);

/**
 * @brief  False once the 8711 has answered "[AT]:ERROR" to either command.
 *
 * True before anything has been tried -- this reports "not known to be
 * unsupported", which is the honest state at boot.  A caller that wants to know
 * for certain has to poll once and look again.
 */
bool wifi_8711_at_data_supported(void);

/** Counters, for answering "is this tunnel doing anything at all?". */
typedef struct
{
    uint32_t polls;         /**< AT+WLRECV commands staged                    */
    uint32_t sends;         /**< AT+WLSEND commands staged                    */
    uint32_t rx_msgs;       /**< inbound messages decoded and delivered       */
    uint32_t rx_bytes;      /**< inbound bytes after hex decoding             */
    uint32_t tx_bytes;      /**< outbound bytes before hex expansion          */
    uint32_t empty_polls;   /**< polls answered "nothing pending"             */
    uint32_t errors;        /**< failed transactions and malformed replies    */
} wifi_8711_data_stats_t;

void wifi_8711_at_data_get_stats(wifi_8711_data_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_8711_AT_DATA_H_ */
