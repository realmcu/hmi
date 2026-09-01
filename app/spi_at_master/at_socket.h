/**
 * @file    at_socket.h
 * @brief   AT+SKT* wrappers, the link_id table, and the routing of inbound socket
 *          bytes to whoever asked for that port.
 *
 * ---------------------------------------------------------------------------
 * THE 8711 IS A NETWORK CARD NOW, NOT A PROTOCOL PEER
 * ---------------------------------------------------------------------------
 * The in-house firmware listened on 5004 and 9000 by itself, parsed the phone's
 * framing, checked CRCs, and handed us fixed-size slots with a full identity in
 * their headers.  The standard command set does none of that.  It gives us a raw
 * TCP byte stream and nothing else:
 *
 *     AT+SKTSERVER=<link>,1,,<port>,1     we create the listener
 *     [$][SKT][EVENT] ... connected ...   a phone opened a connection
 *     [$][SKT][DATA][<seed>][<len>]:...   bytes, verbatim, up to 16 KiB a time
 *
 * So the framing above TCP is ours to find.  This module stops at "which port did
 * these bytes arrive on"; what the bytes mean is the caller's problem.
 *
 * ---------------------------------------------------------------------------
 * TWO KINDS OF link_id, AND ONLY ONE OF THEM IS OURS TO CHOOSE
 * ---------------------------------------------------------------------------
 * A server link is one WE name in the AT+SKTSERVER command -- and we have to
 * remember it, because a successful AT+SKTSERVER answers a bare "OK" and does not
 * echo the id back.
 *
 * A seed link is allocated by the 8711 when a phone connects, and it is announced
 * only in the connect event.  Every [$][SKT][DATA] names the SEED, never the
 * server, so without that event's server/seed pairing there is no way to tell which
 * listener a byte belongs to -- this module supports several even though the eBadge
 * protocol now opens exactly one.  Learning the pairing is therefore not bookkeeping
 * -- it is the routing table, and a missed connect event means the following data is
 * unroutable rather than merely unattributed.
 *
 * ---------------------------------------------------------------------------
 * THE CONNECT EVENT IS ALSO THE JOIN EDGE
 * ---------------------------------------------------------------------------
 * The Wi-Fi command set emits no association events at all (see at_wifi.h), so
 * "a phone is here" has to come from somewhere else, and this is the somewhere:
 * a TCP connection cannot be opened by a station that never associated, and it is
 * closer to what a session actually waits for -- somebody able to send us bytes --
 * than a layer-2 association would be.
 *
 * ---------------------------------------------------------------------------
 * BYTES ARRIVE IN CHUNKS AND THE POINTER DIES WHEN THE CALLBACK RETURNS
 * ---------------------------------------------------------------------------
 * on_data is handed a slice of the SPI slot, not a whole event: a 16 KiB event
 * does not fit one 16376-byte slot payload, and copying it would cost a second
 * 16 KiB buffer for nothing.  So the callback receives (offset, total, is_last)
 * and must consume or copy immediately -- see at_parser.h.
 *
 * CONTEXT: callbacks run on the SPI transport thread.  Anything touching protocol
 * state must hop to l2_task with ebadge_task_post_call(), exactly as the old slot
 * ingress did.
 */
#ifndef _AT_SOCKET_H_
#define _AT_SOCKET_H_

#include <stdint.h>
#include <stdbool.h>

#include "at_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/** link_id range, from lwIP's MEMP_NUM_NETCONN = 20 on the 8711. */
#define AT_SOCKET_LINK_MAX      20

/** The 8711's own receive buffer, and so the largest single [$][SKT][DATA]:
 *  AT_SOCKET_RECEIVE_BUFFER_SIZE = ATCMD_SPI_DMA_SIZE = 16384. */
#define AT_SOCKET_DATA_MAX      16384U

/** Budget for AT+SKTSERVER.  It binds, listens and spawns a receive thread, so it
 *  is slower than a query but nothing about it waits on the network. */
#define AT_SOCKET_OPEN_TIMEOUT_MS   5000U

/** Budget for AT+SKTDEL.  It closes the socket and joins the receive thread,
 *  which is why it is not the default: the thread only notices on its next select
 *  wakeup. */
#define AT_SOCKET_DEL_TIMEOUT_MS    5000U

/*----------------------------------------------------------------------------*
 *  Callbacks
 *----------------------------------------------------------------------------*/

/**
 * @brief  Inbound bytes for a connection accepted by @p server_link.
 *
 * @param  server_link  the listener the caller opened -- i.e. which port
 * @param  seed_link    the 8711's id for this connection, needed to reply to it
 * @param  data         points into the SPI slot; INVALID after this returns
 * @param  len          bytes in this chunk, NOT in the event
 * @param  offset       where this chunk starts within the event
 * @param  total        the event's full length
 * @param  is_last      offset + len == total
 */
typedef void (*at_socket_data_cb_t)(uint8_t server_link, uint8_t seed_link,
                                    const uint8_t *data, uint16_t len,
                                    uint32_t offset, uint32_t total,
                                    bool is_last, void *user);

/**
 * @brief  A connection on @p server_link opened or closed.
 *
 * @param  connected  true for the connect event, false for the disconnect one.
 *                    The 8711 only reports a disconnect for a SEED link (one its
 *                    server accepted), which is the only kind we make.
 */
typedef void (*at_socket_conn_cb_t)(uint8_t server_link, uint8_t seed_link,
                                    bool connected, void *user);

/** @brief  Outcome of an AT+SKTSERVER / AT+SKTDEL. */
typedef void (*at_socket_done_cb_t)(bool ok, int error_no, void *user);

typedef struct
{
    at_socket_data_cb_t on_data;
    at_socket_conn_cb_t on_conn;
    void               *user;
} at_socket_cbs_t;

/*----------------------------------------------------------------------------*
 *  Lifecycle
 *----------------------------------------------------------------------------*/

/**
 * @brief  Initialise the module, the AT client under it, and hook the event and
 *         data sinks.  Idempotent.
 *
 * Takes at_client's single data sink, so nothing else may install one.
 */
int at_socket_init(void);

/**
 * @brief  Forget every link and every callback, without talking to the 8711.
 *
 * For the "ATCMD READY" path only: the 8711 has rebooted, so its sockets are gone
 * and sending AT+SKTDEL for them would be asking about links that no longer
 * exist.  A caller that wants the listeners back must open them again.
 */
void at_socket_reset(void);

/*----------------------------------------------------------------------------*
 *  Listeners
 *----------------------------------------------------------------------------*/

/**
 * @brief  Create a TCP server on @p port with automatic receive.
 *
 * Sends AT+SKTSERVER=<link>,1,,<port>,1.  The five arguments are link_id,
 * conn_type (1 = TCP; 0 would be UDP), an empty certificate slot, the port, and
 * auto_rcv.
 *
 * auto_rcv MUST be 1 and is not a parameter here.  It is the entire data plane:
 * with it the 8711 spawns a receive thread per connection and pushes
 * [$][SKT][DATA] as bytes arrive, and without it the only way to read is
 * AT+SKTREAD -- which is implemented with at_printf("%s", buf) and therefore
 * truncates at the first 0x00.  For JPEG and for EBXF that is every transfer.
 *
 * @param  link  the id to use, [0, AT_SOCKET_LINK_MAX).  Chosen by the caller
 *               because the 8711 does not report it back on success.
 * @param  cbs   copied; must be non-NULL with a non-NULL on_data
 *
 * @retval 0         queued
 * @retval -ENODEV   at_socket_init() never succeeded
 * @retval -EINVAL   link out of range, port 0, or cbs unusable
 * @retval -EADDRINUSE that link is already a server or a seed, or another open
 *                     is on the wire
 * @retval other     as at_client_submitf()
 */
int at_socket_server_open(uint8_t link, uint16_t port,
                          const at_socket_cbs_t *cbs,
                          at_socket_done_cb_t done, void *user);

/**
 * @brief  Delete a link.  Sends AT+SKTDEL=<link>.
 *
 * Deleting a SERVER also destroys every connection hanging off it on the 8711
 * side, and this module forgets those seeds to match -- silently, because it is
 * the documented consequence rather than a fault.
 *
 * Deleting a SEED is how a transfer is cut short; it is what replaces the old
 * firmware's private AT+XFERSTOP.
 *
 * The table entry is forgotten when the command completes, whether or not it was
 * accepted: a refusal nearly always means the link was already gone, and going on
 * believing in it would route the next phone's bytes to a finished session.
 *
 * @retval -ENOENT  @p link is not a server or a seed
 * @retval -EBUSY   another delete is already on the wire
 */
int at_socket_del(uint8_t link, at_socket_done_cb_t done, void *user);

/*----------------------------------------------------------------------------*
 *  Replying
 *----------------------------------------------------------------------------*/

/**
 * @brief  Send @p len bytes on a seed link, binary-safe.
 *
 * Thin wrapper over at_client_send_raw() -- AT+SKTSENDRAW plus transparent
 * transmission -- with the one check the caller cannot make: that @p seed is
 * still a live connection.  Sending to a link the 8711 has already torn down
 * answers ERROR several seconds later, by which time the session has usually
 * moved on.
 *
 * @retval -ENOTCONN  @p seed is not a connection we know about
 */
int at_socket_send_raw(uint8_t seed, const uint8_t *data, uint16_t len,
                       at_socket_done_cb_t done, void *user);

/*----------------------------------------------------------------------------*
 *  Introspection
 *----------------------------------------------------------------------------*/

typedef enum
{
    AT_LINK_FREE = 0,
    AT_LINK_SERVER,
    AT_LINK_SEED,
} at_link_role_t;

typedef struct
{
    at_link_role_t role;
    uint16_t       port;        /**< the listener's port; also copied to its seeds */
    uint8_t        server_link; /**< for a seed: which listener accepted it        */
} at_socket_link_t;

/** Read one table entry.  Returns false for an out-of-range link. */
bool at_socket_link_get(uint8_t link, at_socket_link_t *out);

/**
 * @brief  The newest live connection on @p server_link, or -1.
 *
 * "Newest" rather than "the one": nothing stops a phone opening a second
 * connection, and the reply belongs with the transfer currently running.  Callers
 * that care about a specific connection should keep the seed_link they were
 * given in on_conn.
 */
int at_socket_seed_of(uint8_t server_link);

/** Live connections accepted by @p server_link. */
uint8_t at_socket_seed_count(uint8_t server_link);

typedef struct
{
    uint32_t servers_opened;
    uint32_t seeds_connected;
    uint32_t seeds_closed;
    uint32_t data_events;
    uint32_t data_bytes;
    uint32_t data_unrouted;   /**< a DATA event naming a link we do not know     */
    uint32_t events_unparsed; /**< a [$][SKT][EVENT] line we could not read      */
} at_socket_stats_t;

void at_socket_get_stats(at_socket_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _AT_SOCKET_H_ */
