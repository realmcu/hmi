/**
 * @file    ebadge_port_ble.h
 * @brief   Thin porting shim on top of the HMI GATT ctrl service.
 *
 * The V1.2 protocol needs three services from the platform BLE stack:
 *
 *   1) an ATT write endpoint (CMD char, f48affc1-...) that flows into our
 *      ebadge_task RX queue,
 *   2) a Notify endpoint (EVENT char, f48affc2-...) we call synchronously to
 *      publish protocol packets to the App,
 *   3) CCCD subscription tracking on the EVENT char (so we can drop notifies
 *      when the peer never enabled them -- see design decision 4).
 *
 * This header exposes just those three concerns.  On this SoC we bind them
 * to the existing hmi_ctrl_service (which already implements the 128-bit
 * UUIDs); on other SoCs port_ble.c can be re-implemented against a native
 * Zephyr host stack without touching anything else in app/protocol/.
 */
#ifndef _EBADGE_PORT_BLE_H_
#define _EBADGE_PORT_BLE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Register the CMD/EVENT/STATUS chars, wire callbacks to the
 *         ebadge_task RX pump.  Idempotent.  Call once, after the BLE
 *         stack + GATT server are up.
 */
void ebadge_port_ble_init(void);

/**
 * @brief  True if a peer is currently connected on the HMI ctrl service.
 */
bool ebadge_port_ble_is_connected(void);

/**
 * @brief  True if the peer has enabled CCCD-notify on the EVENT char.
 *         Consulted by ebadge_l2_notify_send() before every emit.
 */
bool ebadge_port_ble_cccd_enabled(void);

/**
 * @brief  Push @p len bytes as a Notify on the EVENT char (fully-framed
 *         eBadge frame).  Called from ebadge_task context only.
 *
 * @return 0 on issue-success, negative on error.
 */
int  ebadge_port_ble_notify(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_BLE_H_ */
