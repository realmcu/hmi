/**
 * @file    hmi_ble_central.h
 * @brief   BLE observer + central role.  The SCANNER half is live; the SENDER
 *          half is behind HMI_CENTRAL_ENABLE_SENDER (default 0).
 *
 * eBadge is a BLE peripheral/GATT-server by default (advertises, phone connects).
 * This module adds the outward-looking side: scan for nearby devices, cache each
 * one's MAC / RSSI / advertised name, and -- when the sender half is enabled --
 * connect as central, discover the peer's HMI ctrl service (128-bit primary,
 * 16-bit chars 0xFFC1 CMD / 0xFFC2 EVENT), enable notifications and push files.
 *
 * WHAT SCANNING COSTS, since it runs alongside advertising rather than instead of
 * it: the scanner is a receiver duty cycle (50 ms interval / 30 ms window, so the
 * radio listens ~60 % of the time while a scan is running) layered on top of a
 * link that already shares one antenna with Wi-Fi.  That is why scanning is NOT
 * started at boot -- see hmi_ble_central_start_scan(), and note that the antenna
 * is parked on BLE by default (ebadge_port_antenna) so a scan during a Wi-Fi
 * transfer is competing for the pad, not just for airtime.
 *
 * The underlying observer/central/GATT-client stack is resident in on-chip ROM
 * (upperstack.a symbol map): le_scan_*, le_connect/le_disconnect, client_*.
 */
#ifndef _HMI_BLE_CENTRAL_H_
#define _HMI_BLE_CENTRAL_H_

#include <stdint.h>
#include <stdbool.h>
#include <gap_le.h>          /* T_LE_SCAN_INFO */

/* The SENDER half of this module (connect -> discover -> push a file) is still
 * bound to the pre-V1.2 protocol, whose implementation lives in app/l2_handlers/
 * -- a directory that is not compiled and not even on the include path.  The
 * SCANNER half (scan / name filter / RSSI cache) has no such dependency: it
 * touches only the ROM-resident observer API and this file's own candidate table.
 *
 * So the two halves are separated by this switch rather than by the file-level
 * exclusion that used to cover both.  Default 0 = scanner only, which is what
 * builds today.  Turning it on requires app/l2_handlers/ to be compiled and on
 * the include path first; until then the sender entry points below are simply
 * absent, and a caller of them fails to link rather than silently doing nothing.
 *
 * See app/bluetooth/hmi_ble/CMakeLists.txt, which no longer removes this file. */
#ifndef HMI_CENTRAL_ENABLE_SENDER
#define HMI_CENTRAL_ENABLE_SENDER   0
#endif

#if HMI_CENTRAL_ENABLE_SENDER
#include "hmi_l2_xfer_client.h"  /* xfer_client_done_cb_t */
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_CENTRAL_MAX_DEVS      8    /* max scan candidates cached per scan
                                        * (one-to-one send needs few; RAM is tight) */
#define HMI_CENTRAL_NAME_LEN      28   /* cached advertised name length
                                        * (adv name field is up to ~26 bytes)        */

/**
 * @brief  Keep only devices whose advertised name contains this string.
 *
 * Define it EMPTY to cache every device the radio reports -- useful for a general
 * "what is around me" scan, and the reason it is a macro rather than a hard-coded
 * strstr(): the peer-eBadge filter is a POLICY of the file-send feature, not a
 * property of scanning.
 *
 * Two things to know before widening it:
 *
 *   - A device with NO parseable Local Name is cached with an empty name rather
 *     than dropped.  It used to be dropped, which was invisible when the filter
 *     was "eBadge" (no name => cannot match anyway) but would silently hide a
 *     large fraction of real devices once the filter is empty: plenty of hardware
 *     advertises its name only in the SCAN RESPONSE, and some never at all.
 *   - HMI_CENTRAL_MAX_DEVS (8) is the ceiling, and in a busy RF environment an
 *     unfiltered scan fills it in well under a second.  The table keeps the FIRST
 *     eight distinct addresses; it does not evict a weak device for a strong one.
 *     Raise it for a survey, but it is 8 * sizeof(T_CEN_DEV) of .bss in a build
 *     whose RAM region is already ~90 % full.
 */
#ifndef HMI_CENTRAL_NAME_FILTER
#define HMI_CENTRAL_NAME_FILTER   "eBadge"
#endif

/*----------------------------------------------------------------------------*
 *  Lifecycle -- call once from bt_task_entry() BEFORE gap_start_bt_stack().
 *----------------------------------------------------------------------------*/
void hmi_ble_central_init(void);

/*----------------------------------------------------------------------------*
 *  UI-facing control (exclusive send mode)
 *----------------------------------------------------------------------------*/
/** Start scanning; clears the previous candidate list.  Advertising KEEPS running
 *  (observer + broadcaster concurrently) -- see the comment in the implementation
 *  for why stopping it here wedges the adv state machine.
 *
 *  NOT called at boot, and that is deliberate: on this stack the first
 *  le_scan_start() of a session can never be undone (the scan wedges at
 *  GAP_SCAN_STATE_START and runs until reset), so starting one at boot would mean
 *  paying the receiver duty cycle for the entire uptime.  Call it when a user
 *  actually opens a device list.  Rejected while a central link is being set up
 *  or is up. */
bool    hmi_ble_central_start_scan(void);
/** Stop accepting scan reports and return to the advertising-only default.
 *  On this stack the radio-level scan cannot actually be stopped once started, so
 *  this is a software gate: reports stop being cached.  See the implementation. */
bool    hmi_ble_central_stop_scan(void);
/** Number of candidates found in the current/last scan (<= HMI_CENTRAL_MAX_DEVS). */
uint8_t hmi_ble_central_get_dev_count(void);
/** Fetch a cached candidate by index; any out-pointer may be NULL.
 *  @param rssi  dBm, as reported by the controller (roughly -30 near, -100 far).
 *  @param name  advertised Local Name, EMPTY STRING if the device did not
 *               advertise one -- absence of a name is normal, not an error. */
bool    hmi_ble_central_get_dev(uint8_t idx, uint8_t bd_addr[6], uint8_t *addr_type,
                                int8_t *rssi, char *name, uint8_t name_len);
/** Stop scanning and connect to candidate @p idx as central. */
bool    hmi_ble_central_connect(uint8_t idx);
/** Actively disconnect the central link. */
bool    hmi_ble_central_disconnect(void);
#if HMI_CENTRAL_ENABLE_SENDER
/** Send a file to the connected peer (only valid once discovery is READY).
 *  @param type  HMI_L2_XFER_TYPE_* ; @param src XIP-mapped source bytes. */
bool    hmi_ble_central_send_file(uint8_t type, const uint8_t *src, uint32_t total,
                                  const char *fname, xfer_client_done_cb_t done_cb);
/** Snapshot the in-progress send (for a UI progress bar); out-params optional.
 *  @return true if a transfer is active.  See hmi_l2_xfer_client_get_progress(). */
bool    hmi_ble_central_get_send_progress(uint32_t *bytes_sent, uint32_t *total,
                                          T_XFER_CLIENT_PHASE *phase);
#endif
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
