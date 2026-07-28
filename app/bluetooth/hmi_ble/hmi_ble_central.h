/**
 * @file    hmi_ble_central.h
 * @brief   BLE central (GATT client) role for device-to-device file send.
 *
 * eBadge is a BLE peripheral/GATT-server by default (advertises, phone connects).
 * This module adds an *exclusive* "send mode": the device stops advertising,
 * scans for peer eBadge units (software filter: local name contains "eBadge"),
 * connects as central, discovers the peer's HMI ctrl service (128-bit primary,
 * 16-bit chars 0xFFC1 CMD / 0xFFC2 EVENT), enables notifications, and is then
 * ready to push the existing xfer L2 frames to the peer (milestone B).
 *
 * Milestone A (this file): scan + name filter + print MAC + connect + discover.
 * On disconnect the device returns to advertising (receiver) state.
 *
 * The underlying observer/central/GATT-client stack is resident in on-chip ROM
 * (upperstack.a symbol map): le_scan_*, le_connect/le_disconnect, client_*.
 */
#ifndef _HMI_BLE_CENTRAL_H_
#define _HMI_BLE_CENTRAL_H_

#include <stdint.h>
#include <stdbool.h>
#include <gap_le.h>          /* T_LE_SCAN_INFO */
#include "hmi_l2_xfer_client.h"  /* xfer_client_done_cb_t */

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_CENTRAL_MAX_DEVS      8    /* max scan candidates cached per scan
                                        * (one-to-one send needs few; RAM is tight) */
#define HMI_CENTRAL_NAME_LEN      28   /* cached advertised name length
                                        * (adv name field is up to ~26 bytes)        */

/*----------------------------------------------------------------------------*
 *  Lifecycle -- call once from bt_task_entry() BEFORE gap_start_bt_stack().
 *----------------------------------------------------------------------------*/
void hmi_ble_central_init(void);

/*----------------------------------------------------------------------------*
 *  UI-facing control (exclusive send mode)
 *----------------------------------------------------------------------------*/
/** Stop advertising and start scanning; clears the previous candidate list. */
bool    hmi_ble_central_start_scan(void);
/** Stop scanning and return to advertising (receiver) state. */
bool    hmi_ble_central_stop_scan(void);
/** Number of "eBadge" candidates found in the current/last scan. */
uint8_t hmi_ble_central_get_dev_count(void);
/** Fetch a cached candidate by index; any out-pointer may be NULL. */
bool    hmi_ble_central_get_dev(uint8_t idx, uint8_t bd_addr[6], uint8_t *addr_type,
                                int8_t *rssi, char *name, uint8_t name_len);
/** Stop scanning and connect to candidate @p idx as central. */
bool    hmi_ble_central_connect(uint8_t idx);
/** Actively disconnect the central link. */
bool    hmi_ble_central_disconnect(void);
/** Send a file to the connected peer (only valid once discovery is READY).
 *  @param type  HMI_L2_XFER_TYPE_* ; @param src XIP-mapped source bytes. */
bool    hmi_ble_central_send_file(uint8_t type, const uint8_t *src, uint32_t total,
                                  const char *fname, xfer_client_done_cb_t done_cb);
/** Snapshot the in-progress send (for a UI progress bar); out-params optional.
 *  @return true if a transfer is active.  See hmi_l2_xfer_client_get_progress(). */
bool    hmi_ble_central_get_send_progress(uint32_t *bytes_sent, uint32_t *total,
                                          T_XFER_CLIENT_PHASE *phase);
/** True while in central (send) mode -- used by the GAP layer to route events. */
bool    hmi_ble_central_is_active(void);
/** True once the link is fully READY (connected, HMI service discovered, notify
 *  enabled) -- the only state in which hmi_ble_central_send_file() is accepted.
 *  The UI must gate the send button / file-list entry on this, NOT on connect()
 *  returning true (which only means "connecting"). */
bool    hmi_ble_central_is_ready(void);

/*----------------------------------------------------------------------------*
 *  Hooks invoked from the GAP callback / message layer
 *----------------------------------------------------------------------------*/
/** From GAP_MSG_LE_SCAN_INFO (hmi_ble_gap_cb.c). */
void hmi_ble_central_handle_scan_info(T_LE_SCAN_INFO *p_info);
/** From GAP_MSG_LE_DEV_STATE_CHANGE: drives the serialized adv-stop -> scan-start. */
void hmi_ble_central_handle_adv_state(uint8_t adv_state);
/** From GAP_MSG_LE_DEV_STATE_CHANGE: defers the fresh le_scan_start() of a
 *  re-scan until the previous scan session has fully stopped (IDLE). */
void hmi_ble_central_handle_scan_state(uint8_t scan_state);
/** From GAP_MSG_LE_CONN_STATE_CHANGE == CONNECTED, when we initiated the link. */
void hmi_ble_central_handle_connected(uint8_t conn_id);
/** From GAP_MSG_LE_CONN_STATE_CHANGE == DISCONNECTED, when we owned the link. */
void hmi_ble_central_handle_disconnected(uint8_t conn_id, uint16_t disc_cause);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_BLE_CENTRAL_H_ */
