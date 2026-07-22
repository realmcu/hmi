/**
 * @file    hmi_l2_xfer_client.h
 * @brief   Device-to-device file SEND (central/GATT-client side of xfer).
 *
 * Mirror of the receiver (hmi_l2_cmd_xfer.c) but as the initiator.  Speaks the
 * SAME wire stack as the phone->device path so the peer eBadge needs no change:
 *
 *   BLE  : write proto frames to peer CMD char (0xFFC1); responses arrive as
 *          notifications on peer EVENT char (0xFFC2).
 *   proto: [0xAB][ver/flags][len BE][crc16 BE][seq BE][payload]  (+ ACK per frame)
 *   L2   : [HMI_L2_CMD_FILE_XFER][ver][key][len_hi][len_lo][value]
 *   xfer : BEGIN_REQ -> BEGIN_RSP -> DATA(seq)... -> END_REQ -> END_RSP
 *
 * A private, self-contained proto codec is used (the shared hmi_proto.c is a
 * singleton already bound to the peripheral transport, so it cannot be reused).
 *
 * Threading: on_notify()/on_write_done() are driven from the central module's
 * GATT-client callbacks (BT stack context); os_timer callback handles timeouts.
 */
#ifndef _HMI_L2_XFER_CLIENT_H_
#define _HMI_L2_XFER_CLIENT_H_

#include <stdint.h>
#include <stdbool.h>
#include <profile_client.h>   /* T_CLIENT_ID, T_GATT_WRITE_TYPE */

#ifdef __cplusplus
extern "C" {
#endif

/** Result reported to the UI when a transfer finishes (see on_done cb). */
typedef enum
{
    XFER_CLIENT_OK = 0,       /* END_RSP OK received                      */
    XFER_CLIENT_ERR_BEGIN,    /* peer rejected BEGIN (busy/no space/type) */
    XFER_CLIENT_ERR_TIMEOUT,  /* handshake / end wait timed out           */
    XFER_CLIENT_ERR_LINK,     /* disconnected mid-transfer                */
    XFER_CLIENT_ERR_ABORT,    /* aborted (local or peer)                  */
    XFER_CLIENT_ERR_STATE,    /* protocol/state error                     */
} T_XFER_CLIENT_RESULT;

/** Completion callback (optional).  progress = bytes sent when result != OK. */
typedef void (*xfer_client_done_cb_t)(T_XFER_CLIENT_RESULT result, uint32_t bytes_sent);

/**
 * @brief  Begin sending @p total bytes from @p src to a connected peer.
 *
 * @param conn_id     GATT-client connection id (from central).
 * @param client_id   Client id registered by central.
 * @param cmd_handle  Peer CMD char (0xFFC1) value handle.
 * @param type        HMI_L2_XFER_TYPE_* (image/video/raw).
 * @param src         Pointer to source bytes (e.g. XIP-mapped FlashDB BigFile).
 * @param total       Total byte count.
 * @param fname       Optional file name (may be NULL / "").
 * @param done_cb     Optional completion callback.
 * @return true if the transfer was started (BEGIN_REQ sent).
 */
bool hmi_l2_xfer_client_start(uint8_t conn_id, T_CLIENT_ID client_id, uint16_t cmd_handle,
                              uint8_t type, const uint8_t *src, uint32_t total,
                              const char *fname, xfer_client_done_cb_t done_cb);

/** Abort an in-progress transfer (sends XFER_ABORT to the peer). */
void hmi_l2_xfer_client_abort(void);

/** True while a transfer is active. */
bool hmi_l2_xfer_client_busy(void);

/*----------------------------------------------------------------------------*
 *  Hooks driven by the central (GATT-client) layer
 *----------------------------------------------------------------------------*/
/** Peer notification on EVENT char (0xFFC2): proto frame bytes. */
void hmi_l2_xfer_client_on_notify(const uint8_t *data, uint16_t len);
/** ATT write completion for the CMD char (paces DATA / confirms delivery).
 *  @param write_type  GATT_WRITE_TYPE_* of the completed write. */
void hmi_l2_xfer_client_on_write_done(uint8_t write_type, uint16_t handle, uint16_t cause);
/** Link dropped: fail any active transfer. */
void hmi_l2_xfer_client_on_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_XFER_CLIENT_H_ */
