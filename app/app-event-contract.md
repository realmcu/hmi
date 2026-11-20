# Bluetooth smartwatch app-layer event contract

> (Planned) for defined or suggested event IDs; the code is not implemented yet.

## app_time

Clock / RTC / time calibration.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_time | `EVT_TIME_TICK_15MIN` (`0x0302`) | On the RTC second interrupt when the wall clock reaches :00/:15/:30/:45; payload is the boundary wall-clock second |
| app_time | `EVT_TIME_DAY_CHANGED` (`0x0303`) | On the RTC second interrupt at wall clock 00:00:00; midnight is published before the 15MIN tick |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_time | — | — | — |

## app_power

Battery level and charging state.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_power | `EVT_POWER_LEVEL` (`0x0200`) | Published when the battery percentage changes |
| app_power | `EVT_POWER_LOW` (`0x0201`) | Published when it drops below 20%, edge-triggered |
| app_power | `EVT_POWER_CHARGING` (`0x0202`) | Published when the charging state changes |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_power | — | — | — |

## app_setting

KV semantic layer for settings.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_setting | `EVT_SETTING_CHANGED` (`0x0400`) | (Planned) published after a setting is written successfully; payload is the key id |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_setting | — | — | — |

## app_bluetooth

BLE and BR/EDR connectivity.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_bluetooth | `EVT_BLE_CONNECTED` (`0x0100`) | Published by the connection callback after a BLE connection is established |
| app_bluetooth | `EVT_BLE_DISCONNECTED` (`0x0101`) | Published by the connection callback when BLE disconnects |
| app_bluetooth | `EVT_BLE_CONN_PARAM` (`0x0102`) | Published on every connection parameter update, not on disconnect |
| app_bluetooth | `EVT_BLE_PAIRING_REQ` (`0x0103`) | (Planned) published on a pairing request; the UI shows the pairing code |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_bluetooth | — | — | — |

## app_protocol

L2 protocol command handling (hmi_l2).

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_protocol | `EVT_TIME_SYNCED` (`0x0300`) | The phone sends HMI_L2_SET_TIME; published after `app_time_set()` writes the RTC successfully |
| app_protocol | `EVT_USER_BOUND` (`0x0A00`) | Published after the binding handshake succeeds; no payload |
| app_protocol | `EVT_USER_UNBOUND` (`0x0A01`) | Published after HMI_L2_UNBIND is received; no payload |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_protocol | `EVT_BLE_CONN_PARAM` (`0x0102`) | `on_ble_conn_param` | Cache the latest connection parameters for L2 query responses |
| app_protocol | `EVT_BLE_DISCONNECTED` (`0x0101`) | `on_ble_disconnected` (conn_param) | Clear the cached connection parameters |
| app_protocol | `EVT_BLE_DISCONNECTED` (`0x0101`) | `on_ble_disconnected` (sport) | Discard the sport sync session |
| app_protocol | `EVT_BLE_DISCONNECTED` (`0x0101`) | `on_ble_disconnected` (xfer) | Abort the file transfer and reset |

## app_health

Step collection, persistence, and daily aggregation.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_health | `EVT_HEALTH_STEPS_UPDATED` (`0x0500`) | Published when the daily health summary changes, at most 1 Hz; also published on start, reset, and TSDB persistence |
| app_health | `EVT_HEALTH_HR_UPDATED` (`0x0501`) | (Planned) published on heart-rate measurement updates |
| app_health | `EVT_HEALTH_SPO2_UPDATED` (`0x0502`) | (Planned) published on SpO2 measurement updates |
| app_health | `EVT_HEALTH_SLEEP_UPDATED` (`0x0503`) | (Planned) published on sleep data updates |
| app_health | `EVT_HEALTH_BP_UPDATED` (`0x0504`) | (Planned) published on blood-pressure measurement updates |
| app_health | `EVT_HEALTH_STRESS_UPDATED` (`0x0505`) | (Planned) published on stress measurement updates |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_health | `EVT_TIME_SYNCED` (`0x0300`) | `on_evt_time_synced` | Record that time calibration completed and try to start the worker |
| app_health | `EVT_USER_BOUND` (`0x0A00`) | `on_evt_user_bound` | Record that binding completed and try to start the worker |
| app_health | `EVT_USER_UNBOUND` (`0x0A01`) | `on_evt_user_unbound` | Stop the worker and clear the day's data |
| app_health | `EVT_POWER_LOW` (`0x0201`) | `on_evt_power_low` | Flush first, then stop the worker |
| app_health | `EVT_TIME_TICK_15MIN` (`0x0302`) | `on_evt_time_tick_15min` | Advance the 15-minute bucket using the boundary time in the payload |
| app_health | `EVT_TIME_DAY_CHANGED` (`0x0303`) | `on_evt_time_day_changed` | Zero the day's cumulative total |

## app_media

A2DP / AVRCP media control.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_media | `EVT_MEDIA_STATE_CHANGED` (`0x0600`) | Published when the AVRCP playback state changes; payload is the `uint8_t` state |
| app_media | `EVT_MEDIA_METADATA` (`0x0601`) | Published when AVRCP track info changes; no payload, consumers read it through the getter |
| app_media | `EVT_MEDIA_VOLUME_CHANGED` (`0x0602`) | Published when the AVRCP absolute volume changes; payload is a percentage |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_media | — | — | — |

## app_notify

Notification caching and dispatch.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_notify | `EVT_NOTIFY_NEW` (`0x0800`) | (Planned) published when a new notification arrives; payload is the notification id |
| app_notify | `EVT_NOTIFY_REMOVED` (`0x0801`) | (Planned) published when a single notification is withdrawn |
| app_notify | `EVT_NOTIFY_CLEARED` (`0x0802`) | (Planned) published when all notifications are cleared |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_notify | — | — | — |

## app_ota

BLE DFU / OTA service.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_ota | — | — |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_ota | — | — | — |

## app_phone

Call state.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_phone | `EVT_PHONE_STATE_CHANGED` (`0x0700`) | Published when HFP state changes such as incoming, outgoing, answered, or hung up |
| app_phone | `EVT_PHONE_RING` (`0x0701`) | Published once per new incoming call; no payload |
| app_phone | `EVT_PHONE_NUMBER_CHANGED` (`0x0702`) | Published when the incoming or dialed number changes; no payload |
| app_phone | `EVT_PHONE_MUTED_CHANGED` (`0x0703`) | Published when the microphone mute state changes; payload is a `bool` |
| app_phone | `EVT_PHONE_ROUTE_CHANGED` (`0x0704`) | Published when the call audio route changes; payload is a `bool`, `true` meaning the local device |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_phone | — | — | — |

## app_display

Display on/off, AOD, and brightness.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_display | `EVT_DISPLAY_SLEEP` (`0x0B00`) | (Planned) published on timeout or manual screen-off |
| app_display | `EVT_DISPLAY_WAKE` (`0x0B01`) | (Planned) published when the screen wakes on a key press, touch, or wrist raise |
| app_display | `EVT_DISPLAY_AOD` (`0x0B02`) | (Planned) published when entering or leaving AOD |
| app_display | `EVT_DISPLAY_BRIGHTNESS` (`0x0B03`) | (Planned) published after a brightness adjustment; payload is a percentage |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_display | — | — | — |

## app_alarm

Alarms, countdown timers, and hourly chimes.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_alarm | `EVT_ALARM_FIRED` (`0x0C00`) | (Planned) published when an alarm fires |
| app_alarm | `EVT_TIMER_DONE` (`0x0C01`) | (Planned) published when a countdown ends |
| app_alarm | `EVT_CHIME` (`0x0C02`) | (Planned) published when the hourly chime triggers |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_alarm | — | — | — |

## app_motion

Gesture and wrist-raise detection.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_motion | `EVT_MOTION_GESTURE` (`0x0D00`) | (Planned) published when gestures such as flip or shake are detected |
| app_motion | `EVT_MOTION_WRIST_RAISE` (`0x0D01`) | (Planned) published on wrist-raise wake-up |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_motion | — | — | — |

## app_workout

Workout records and real-time data.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_workout | `EVT_WORKOUT_STATE_CHANGED` (`0x0E00`) | (Planned) published when a workout starts, pauses, resumes, or ends |
| app_workout | `EVT_WORKOUT_DATA_UPDATED` (`0x0E01`) | (Planned) published when real-time workout data updates |
| app_workout | `EVT_WORKOUT_GOAL_REACHED` (`0x0E02`) | (Planned) published when a goal is reached |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_workout | — | — | — |

## app_weather

Weather information.

### Publish

| Module | Published event | Publish timing |
| --- | --- | --- |
| app_weather | `EVT_WEATHER_UPDATED` (`0x0F00`) | (Planned) published after the phone pushes weather data |

### Subscribe

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_weather | — | — | — |
