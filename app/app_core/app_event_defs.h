#ifndef __APP_EVENT_DEFS_H__
#define __APP_EVENT_DEFS_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_event_defs.h
 * @brief Cross-module event id enum and payload structs.
 *
 * Convention: event ids are partitioned by owning module in 0x100 chunks
 * so a numeric id points to its source at a glance. Payloads that don't
 * fit @c APP_EVENT_MAX_PAYLOAD (32 bytes) are exposed via getters on the
 * owning module — this file only holds compact structs and scalars.
 *
 * The 0x900 range is intentionally left blank as a placeholder for a
 * future OTA module (out of scope for the current skeleton).
 */

enum
{
    /* BLE (0x100~) */
    EVT_BLE_CONNECTED        = 0x0100, /* payload: app_evt_ble_connected_t */
    EVT_BLE_DISCONNECTED     = 0x0101, /* payload: app_evt_ble_disconnected_t */
    EVT_BLE_CONN_PARAM       = 0x0102, /* payload: app_evt_ble_conn_param_t */

    /* Power (0x200~) */
    EVT_POWER_LEVEL          = 0x0200, /* payload: app_evt_power_level_t */
    EVT_POWER_LOW            = 0x0201, /* payload: none, edge-triggered when crossing low threshold */
    EVT_POWER_CHARGING       = 0x0202, /* payload: app_evt_power_charging_t */

    /* Time (0x300~) */
    EVT_TIME_SYNCED          = 0x0300, /* payload: none, wall clock has been set from phone */
    EVT_TIME_TICK_MIN        = 0x0301, /* payload: none, one tick per local minute */

    /* Setting (0x400~) */
    EVT_SETTING_CHANGED      = 0x0400, /* payload: app_evt_setting_changed_t */

    /* Health (0x500~) */
    EVT_HEALTH_STEPS_UPDATED = 0x0500, /* payload: uint32_t steps */
    EVT_HEALTH_HR_UPDATED    = 0x0501, /* payload: uint8_t  bpm */
    EVT_HEALTH_SPO2_UPDATED  = 0x0502, /* payload: uint8_t  percent */

    /* Media (0x600~) */
    EVT_MEDIA_STATE_CHANGED  = 0x0600, /* payload: uint8_t (app_media_state_t) */
    EVT_MEDIA_METADATA       = 0x0601, /* payload: none, read via app_media_track_*() */
    EVT_MEDIA_VOLUME_CHANGED = 0x0602, /* payload: uint8_t percent */

    /* Phone (0x700~) */
    EVT_PHONE_STATE_CHANGED  = 0x0700, /* payload: uint8_t (app_phone_state_t) */
    EVT_PHONE_RING           = 0x0701, /* payload: none, emitted once per new incoming call */

    /* Notify (0x800~) */
    EVT_NOTIFY_NEW           = 0x0800, /* payload: uint32_t id, details via app_notify_get() */
    EVT_NOTIFY_REMOVED       = 0x0801, /* payload: uint32_t id */
    EVT_NOTIFY_CLEARED       = 0x0802, /* payload: none */

    /* 0x900~ reserved for future OTA */
};

/* -------- Payload structs -------- */

typedef struct
{
    uint8_t  conn_id;      /* Realtek GAP conn_id assigned by the stack */
    uint16_t conn_handle;  /* ATT/HCI connection handle */
} app_evt_ble_connected_t;

typedef struct
{
    uint8_t  conn_id;
    uint16_t reason;       /* HCI disc_cause, e.g. HCI_ERR | HCI_ERR_REMOTE_USER_TERMINATE */
} app_evt_ble_disconnected_t;

/**
 * @brief  Latest negotiated BLE connection parameters.
 *
 * Published on every successful parameter update (initial connection and
 * subsequent GAP_MSG_LE_CONN_PARAM_UPDATE), NOT on disconnect — subscribers
 * that need "disconnected" semantics should listen to @c EVT_BLE_DISCONNECTED
 * instead of watching for zeroed params here.
 */
typedef struct
{
    uint16_t conn_interval;             /* units: 1.25 ms */
    uint16_t conn_latency;              /* slave latency  */
    uint16_t conn_supervision_timeout;  /* units: 10 ms   */
    uint16_t conn_mtu_size;             /* ATT MTU, bytes */
} app_evt_ble_conn_param_t;

typedef struct { uint8_t percent;  } app_evt_power_level_t;
typedef struct { bool    charging; } app_evt_power_charging_t;

/**
 * @brief Setting key id.
 *
 * @c EVT_SETTING_CHANGED carries a key id rather than a string pointer,
 * because event delivery is asynchronous and string ownership would be
 * awkward. Each new setting adds one enumerator here and one row in the
 * @c app_setting.c internal key-name table.
 */
typedef enum
{
    APP_SETTING_KEY_UNKNOWN = 0,
    /* Business keys added here as they land. */
} app_setting_key_id_t;

typedef struct { app_setting_key_id_t key; } app_evt_setting_changed_t;

#ifdef __cplusplus
}
#endif

#endif /* __APP_EVENT_DEFS_H__ */
