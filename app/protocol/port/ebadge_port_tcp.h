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
 * @section result  WHY THE RESULT IS A STATUS AND A REASON, NOT A BUFFER
 *
 * The interface carries the *meaning* -- succeeded or not, and why -- and lets the
 * transport choose the framing.  It briefly went the other way: an intermediate
 * firmware built the EBXR itself from a status and a reason we handed it over
 * AT+XFERACK, so passing pre-packed bytes through would have needed an encoder no
 * transport used.  That firmware is gone and the 8711 frames nothing now -- the
 * implementation packs EBXR with ebxr_pack() and pushes it with AT+SKTSENDRAW --
 * but the signature is kept, because a transport that owns a real socket would
 * pack it differently and neither choice should leak into the sessions.
 *
 * @section abort  ACK AND ABORT ARE NOT THE SAME OPERATION
 *
 * ack() delivers a verdict and lets the connection finish.  abort() cuts it
 * without a verdict, and exists for the case where the remaining inbound bytes
 * are only wasting time -- a header that contradicts the BLE offer, a storage
 * failure mid-write.  They stay separate because "reject this file" and "stop
 * sending" are different requests, and a stream can be worth stopping long before
 * there is any result to report.
 *
 * There is also no deadline on the wire any more.  The old firmware gave us a
 * 120 s window to answer; nothing replaced it, so what bounds the verdict now is
 * the phone's own patience (XS_ACK_WAIT_MS bounds only our wait for the ack to
 * settle locally).
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
 * @brief  Bring the socket layer up.  Call once from ebadge_task_init(), before
 *         anything can call listen().
 *
 * WITHOUT THIS EVERY listen() IS REFUSED -ENODEV AND NO INBOUND BYTE ARRIVES.
 * at_socket_init() is what registers the [$][SKT][DATA] and [$][SKT][EVENT] sinks
 * on at_client, so it is not merely a flag: skipping it costs the data callback,
 * the connect edge that stands in for "a station joined", and the reset hook that
 * forgets links when the 8711 reboots.  It was missing for exactly one release and
 * the symptom was a transfer that got as far as
 *
 *     port_tcp: SKTSERVER port=9000 could not be queued rc=-19
 *     xfer: tcp_listen FAIL -> XFER_FAIL AP_START
 *
 * i.e. a Wi-Fi fault reported for a radio that was perfectly fine.  Bringing up
 * the AP is NOT enough on its own: ebadge_port_softap_init() calls at_wifi_init()
 * and both of those bottom out in the same at_client_init(), so the transport and
 * the parser are already alive by then -- which is what makes this omission so
 * quiet.  at_socket keeps its own s_inited, and only this reaches it.
 *
 * Puts nothing on the wire.  The listeners are created per session by listen(),
 * because the port comes from ebadge_port_softap_start().  Idempotent.
 */
void ebadge_port_tcp_init(void);

/**
 * @brief  Create BOTH TCP servers (file and preview) on the 8711.
 *
 * Call this the moment AT+WLSTARTAP answers OK -- ebadge_port_softap.c does, from
 * the start completion -- because a listener is only reachable while the interface
 * it is bound to exists, and that interface is the AP.  The two are one action.
 *
 * WHY NOT AT OFFER TIME, WHICH IS WHERE THIS USED TO HAPPEN.  AT+SKTSERVER is a
 * round trip on a single-flight AT link, and at offer time it lands in the worst
 * possible place: between the phone being told ACCEPT and being told where to
 * connect.  It shared that window with the 0x13 AP_INFO, so the phone could be
 * handed a port whose socket had not been created yet, and any delay -- a queue
 * four deep, a retry, a 25 s WLSTARTAP still draining -- pushed the socket's
 * creation past the phone's first connect attempt.  Opening both doors when the
 * radio comes up removes the race instead of narrowing it: by the time any offer is
 * answered, both ports have either been listening for seconds or failed loudly.
 *
 * Both ports, not the one the imminent session needs, because which session comes
 * first is not knowable here and an AT round trip per offer is what we are removing.
 * An unused listener costs one socket and one receive thread on the 8711.
 *
 * Idempotent and asynchronous: a link already open, or with an open on the wire, is
 * skipped, so calling it on every arm is free.  Nothing here reports failure to a
 * caller -- ebadge_port_tcp_listen() is where a missing server surfaces, and it
 * surfaces as the AP_START the session already knows how to report.
 */
void ebadge_port_tcp_open_servers(void);

/**
 * @brief  Destroy both servers.  Call BEFORE taking the radio down.
 *
 * Ordering matters and is the reason this is separate from close(): stopping the AP
 * destroys the interface the sockets are bound to, and at_socket's link table would
 * go on believing in them -- so the next AT+SKTSERVER would be refused
 * -EADDRINUSE for a server the 8711 no longer has, on a link id it could then
 * never reuse.
 *
 * Also disarms any session sink, because a vanished interface cannot deliver bytes
 * and a sink left armed would only outlive the session that owns it.
 *
 * Idempotent.  Fire and forget.
 */
void ebadge_port_tcp_close_servers(void);

/**
 * @brief  Arm a session's sink on the listener for @p cfg->port.
 *
 * Does NOT create the socket any more -- ebadge_port_tcp_open_servers() did that
 * when the AP came up.  What this does is say which session the bytes arriving on
 * that port belong to, so it is cheap, synchronous and cannot fail on the wire.
 *
 * Single-session: a second call replaces the first, whichever port it names.  Bytes
 * arriving on the OTHER port are dropped while this one is armed, which is what
 * keeps a preview frame out of a file session's accumulator.
 *
 * @retval 0        armed
 * @retval -ENETDOWN  no server on that port -- the AP never came up, or its
 *                    AT+SKTSERVER failed.  Sessions map this to AP_START.
 *
 * IF A PHONE IS ALREADY CONNECTED to that port when this is called, the joined edge
 * is delivered here rather than lost: the doors now open before the offer does, so
 * "connected already" is reachable in a way it was not when listen() created the
 * socket itself.
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

/** Close the connection and disarm this session's sink.  Idempotent.
 *
 *  Does NOT destroy the listeners -- they belong to the AP's lifetime now, not to a
 *  session's, and ebadge_port_tcp_close_servers() is what takes them down.  This is
 *  the difference that lets a second transfer on one BLE connection run without
 *  another AT+SKTSERVER: the door stays open between sessions. */
int  ebadge_port_tcp_close(void);

/**
 * @brief  True when a peer connection exists to ack() or abort().
 *
 * NOT for the sessions -- they know whether they have a transfer in flight, and
 * asking this instead would be asking the transport to confirm what the session
 * already decided.  It exists because ack() and abort() both return 0 when there
 * is nothing to act on (deliberately: a missing connection is not a transfer
 * failure, BLE carries the real verdict), which leaves a caller that wants to
 * REPORT "there was nothing to do" with no way to tell.  cmd_debug's hand-driven
 * subcmds are that caller.
 */
bool ebadge_port_tcp_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_TCP_H_ */
