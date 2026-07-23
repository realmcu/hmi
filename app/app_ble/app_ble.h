#ifndef __APP_BLE_H__
#define __APP_BLE_H__

#include "app_module.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_ble.h
 * @brief Bluetooth stack ownership and connection state.
 *
 * Owns Realtek BLE MGR bring-up and callback plumbing; translates stack
 * events into app-layer events (@c EVT_BLE_CONNECTED / @c EVT_BLE_DISCONNECTED).
 * Also the mount point for the private GATT service used by the
 * companion phone app — those live in future sibling files inside
 * @c app_ble/ (e.g. @c gatt_watch_service.c ). Not part of the skeleton.
 */
extern const app_module_t app_ble_module;

/** @return true if a phone is currently connected. */
bool app_ble_is_connected(void);

/** Actively drop the current connection (e.g. user chose "forget device"). */
int  app_ble_disconnect(void);

/** Advertise controls, used by power / notify to modulate discoverability. */
int  app_ble_advertising_start(void);
int  app_ble_advertising_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_BLE_H__ */
