/**
 * @file    ebadge_port_tcp.h
 * @brief   Byte-stream endpoint for the file-transfer data plane.
 *
 * NOT a socket abstraction -- it is deliberately weaker.  The transport may
 * be a lwIP TCP listener, a socket bridge over UART/SPI, or an AT-command
 * shim to an external Wi-Fi chip.  All the protocol stack needs is:
 *
 *   listen(port)  -> arm the endpoint, register on_data / on_close
 *   send(bytes)   -> emit some bytes to the connected peer (EBXR ack)
 *   close()       -> tear down after done / fail
 *
 * @section contract  on_data delivery contract
 *
 * Implementations MUST batch inbound bytes to reasonably-sized callbacks
 * (target: 4 KB per call).  Each on_data() triggers a memcpy + msg queue
 * post into l2_task; delivering byte-by-byte would flood the queue.  It is
 * always safe to deliver MORE than 4 KB in one call.
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
 * @brief  Push @p len bytes to the connected peer (typically the 8-byte
 *         EBXR ack frame).  May be called from l2_task.
 */
int  ebadge_port_tcp_send(const uint8_t *data, uint16_t len);

/** Close the connection and stop listening.  Idempotent. */
int  ebadge_port_tcp_close(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_TCP_H_ */
