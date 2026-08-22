/**
 * @file    ebadge_port_tcp.h
 * @brief   Data-plane endpoint for the file transfer.
 *
 * NOT a socket abstraction -- it is deliberately weaker.  The transport may be a
 * lwIP TCP listener, a socket bridge over UART/SPI, or an AT-command shim to an
 * external Wi-Fi chip.  All the protocol stack needs is:
 *
 *   listen(port)          arm the endpoint, register on_data / on_close
 *   ack(status, reason)   tell the peer the business result (EBXR)
 *   abort()               cut the connection now, with no result
 *   close()               tear down after done / fail
 *
 * @section result  WHY THE RESULT IS TWO BYTES AND NOT A BUFFER
 *
 * This used to be send(bytes), with the caller packing the 8-byte EBXR frame
 * itself.  Protocol v2.2 (SPI spec sec.6.3) took the framing away from us: the
 * Wi-Fi chip builds the EBXR from a status and a reason we hand it, then closes
 * the connection.  Passing pre-packed bytes through would need a second encoder
 * on this side that no transport actually uses.
 *
 * So the interface now carries the *meaning* -- succeeded or not, and why -- and
 * lets the transport choose the framing.  A transport that really owns a socket
 * can pack EBXR with ebxr_pack() and write it; the 8711 one issues an AT
 * command.  Neither leaks into the sessions.
 *
 * @section abort  ACK AND ABORT ARE NOT THE SAME OPERATION
 *
 * ack() delivers a verdict and lets the connection finish.  abort() cuts it
 * without a verdict, and exists for the case where the remaining inbound bytes
 * are only wasting time -- a header that contradicts the BLE offer, a storage
 * failure mid-write.  On the 8711 the two are separate commands (AT+XFERACK vs
 * AT+XFERSTOP) precisely because "reject this file" and "stop sending" are
 * different requests, and a stream can be worth stopping long before there is
 * any result to report.
 *
 * After abort() the peer gets no coded reason, so the caller must still emit the
 * BLE 0x16 XFER_FAIL -- which is the authoritative plane anyway (eBadge spec
 * sec.5.3: TCP status does not replace BLE 0x15/0x16).
 *
 * @section contract  on_data delivery contract
 *
 * Implementations MUST batch inbound bytes to reasonably-sized callbacks
 * (target: 4 KB per call).  Each on_data() triggers a memcpy + msg queue post
 * into l2_task; delivering byte-by-byte would flood the queue.  It is always
 * safe to deliver MORE than 4 KB in one call.
 */
#ifndef _EBADGE_PORT_TCP_H_
#define _EBADGE_PORT_TCP_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Reason codes for on_close_cb. */
typedef enum
{
    EBADGE_TCP_CLOSE_LOCAL   = 0,   /* we called port_tcp_close()          */
    EBADGE_TCP_CLOSE_PEER    = 1,   /* peer closed the connection normally */
    EBADGE_TCP_CLOSE_TIMEOUT = 2,   /* no bytes for too long               */
    EBADGE_TCP_CLOSE_ERROR   = 3,   /* transport / link error              */
} ebadge_tcp_close_reason_t;

/** Fires from the transport thread; MUST post_call into l2_task before
 *  touching xfer_session state.  Caller retains ownership of @p data. */
typedef void (*ebadge_tcp_on_data_cb_t)(const uint8_t *data, uint16_t len);

/** Fires from the transport thread; MUST post_call into l2_task. */
typedef void (*ebadge_tcp_on_close_cb_t)(ebadge_tcp_close_reason_t reason);

/**
 * @brief  Fires once the result handed to ebadge_port_tcp_ack() has been dealt
 *         with, one way or the other.
 *
 * @param  ok    true if the transport confirmed it delivered the result; false
 *               if it was refused, timed out, or abandoned.  Either way this is
 *               the moment after which no further attempt will be made.
 * @param  user  the pointer handed to ack()
 *
 * GUARANTEED to fire exactly once per ack(), and NEVER synchronously from inside
 * ack() -- so a caller may stage state before the call and rely on it still
 * being there afterwards.  That matters because the session uses this to order
 * its BLE verdict behind the data-plane one, and a re-entrant callback would
 * emit the verdict from inside the function that was setting it up.
 *
 * Fires from the transport thread or the system workqueue, never l2_task, so it
 * MUST post_call before touching session state.
 */
typedef void (*ebadge_tcp_ack_done_cb_t)(bool ok, void *user);

typedef struct
{
    uint16_t                port;
    ebadge_tcp_on_data_cb_t on_data;
    ebadge_tcp_on_close_cb_t on_close;
} ebadge_tcp_listen_t;

/**
 * @brief  Bind to @p cfg->port, accept ONE inbound connection, feed bytes
 *         via on_data.  Single-session; a second call replaces the first.
 */
int  ebadge_port_tcp_listen(const ebadge_tcp_listen_t *cfg);

/**
 * @brief  Report the transfer's business result to the peer.
 *
 * @param  ok      true when length, CRC and any storage commit all succeeded
 * @param  reason  eBadge spec sec.2.6 transfer error code; ignored (and must be
 *                 0) when @p ok is true
 * @param  done    optional; fires once the result has been delivered or given up
 *                 on.  Pass NULL to fire and forget.
 *
 * Asynchronous and best effort: 0 means the result was handed to the transport,
 * not that the peer received it.  Wait for @p done to learn that.  The transport
 * typically also closes the connection as part of delivering the result.
 *
 * Call this only once per session, and only after the verdict is final -- on the
 * 8711 it is what makes the phone's transfer end with a code instead of a bare
 * disconnect, and the chip only waits a bounded time for it.
 *
 * @p done still fires when the return value is <0 or when there is no transport
 * at all, so a caller sequencing work behind it cannot be left waiting forever.
 *
 * @retval 0   handed over (or dropped harmlessly -- see the impl)
 * @retval <0  the result could not be delivered at all
 */
int  ebadge_port_tcp_ack(bool ok, uint8_t reason,
                         ebadge_tcp_ack_done_cb_t done, void *user);

/**
 * @brief  Cut the connection immediately, delivering no result.
 *
 * For when continuing to receive is pointless.  The peer learns nothing from
 * this, so the caller must report the failure over BLE.
 *
 * Idempotent and best effort; safe to call with no connection up.
 */
int  ebadge_port_tcp_abort(void);

/** Close the connection and stop listening.  Idempotent. */
int  ebadge_port_tcp_close(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_TCP_H_ */
