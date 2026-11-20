# Bluetooth smartwatch app-layer event contract

> (planned) Event IDs that are defined or proposed; the code is not implemented yet.

## app_time

Clock / RTC / time sync.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_time | `EVT_TIME_TICK_15MIN` (`0x0302`) | RTC second interrupt, fired at wall-clock :00/:15/:30/:45; the payload is the boundary wall-clock second |
| app_time | `EVT_TIME_DAY_CHANGED` (`0x0303`) | RTC second interrupt, fired at wall-clock 00:00:00; midnight is published before 15MIN |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_time | — | — | — |

## app_power

Battery and charging state.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_power | `EVT_POWER_LEVEL` (`0x0200`) | Published when the battery percentage changes |
| app_power | `EVT_POWER_LOW` (`0x0201`) | Published when it drops below 20%, edge triggered |
| app_power | `EVT_POWER_CHARGING` (`0x0202`) | Published when the charging state changes |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_power | — | — | — |

## app_setting

KV semantics layer for settings.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_setting | `EVT_SETTING_CHANGED` (`0x0400`) | (planned) Published after a setting is written successfully; the payload is the key id |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_setting | — | — | — |

## app_bluetooth

BLE and BR/EDR access.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_bluetooth | `EVT_BLE_CONNECTED` (`0x0100`) | Published by the connection callback once the BLE link is up |
| app_bluetooth | `EVT_BLE_DISCONNECTED` (`0x0101`) | Published by the connection callback when BLE disconnects |
| app_bluetooth | `EVT_BLE_CONN_PARAM` (`0x0102`) | Published on every connection parameter update; not published on disconnect |
| app_bluetooth | `EVT_BLE_PAIRING_REQ` (`0x0103`) | (planned) Published on a pairing request; the UI shows the pairing code |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_bluetooth | — | — | — |

## app_protocol

L2 protocol command handling (hmi_l2).

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_protocol | `EVT_TIME_SYNCED` (`0x0300`) | Phone sends HMI_L2_SET_TIME; published after `app_time_set()` writes the RTC successfully |
| app_protocol | `EVT_USER_BOUND` (`0x0A00`) | Published after a successful bind handshake, no payload |
| app_protocol | `EVT_USER_UNBOUND` (`0x0A01`) | Published after HMI_L2_UNBIND is received, no payload |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_protocol | `EVT_BLE_CONN_PARAM` (`0x0102`) | `on_ble_conn_param` | Cache the latest connection parameters for L2 query replies |
| app_protocol | `EVT_BLE_DISCONNECTED` (`0x0101`) | `on_ble_disconnected` (conn_param) | Clear the cached connection parameters |
| app_protocol | `EVT_BLE_DISCONNECTED` (`0x0101`) | `on_ble_disconnected` (sport) | Drop the sport sync session |
| app_protocol | `EVT_BLE_DISCONNECTED` (`0x0101`) | `on_ble_disconnected` (xfer) | Abort the file transfer and reset |

## app_health

Step acquisition, storage and daily aggregation.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_health | `EVT_HEALTH_STEPS_UPDATED` (`0x0500`) | Published when the daily health summary changes, at most 1 Hz; also published on startup, reset to zero and TSDB persist |
| app_health | `EVT_HEALTH_HR_UPDATED` (`0x0501`) | (planned) Published on a heart-rate measurement update |
| app_health | `EVT_HEALTH_SPO2_UPDATED` (`0x0502`) | (planned) Published on a SpO2 measurement update |
| app_health | `EVT_HEALTH_SLEEP_UPDATED` (`0x0503`) | (planned) Published on a sleep data update |
| app_health | `EVT_HEALTH_BP_UPDATED` (`0x0504`) | (planned) Published on a blood-pressure measurement update |
| app_health | `EVT_HEALTH_STRESS_UPDATED` (`0x0505`) | (planned) Published on a stress measurement update |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_health | `EVT_TIME_SYNCED` (`0x0300`) | `on_evt_time_synced` | Record that time sync finished; try to start the worker |
| app_health | `EVT_USER_BOUND` (`0x0A00`) | `on_evt_user_bound` | Record the bound state; try to start the worker |
| app_health | `EVT_USER_UNBOUND` (`0x0A01`) | `on_evt_user_unbound` | Stop the worker and clear the daily data |
| app_health | `EVT_POWER_LOW` (`0x0201`) | `on_evt_power_low` | Flush first, then stop the worker |
| app_health | `EVT_TIME_TICK_15MIN` (`0x0302`) | `on_evt_time_tick_15min` | Advance the 15-minute bucket using the boundary time from the payload |
| app_health | `EVT_TIME_DAY_CHANGED` (`0x0303`) | `on_evt_time_day_changed` | Zero the daily accumulators |

## app_media

A2DP / AVRCP media control.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_media | `EVT_MEDIA_STATE_CHANGED` (`0x0600`) | (planned) Published when the playback state changes |
| app_media | `EVT_MEDIA_METADATA` (`0x0601`) | (planned) Published when the track metadata changes, no payload |
| app_media | `EVT_MEDIA_VOLUME_CHANGED` (`0x0602`) | (planned) Published when the volume changes |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_media | — | — | — |

## app_notify

Notification caching and dispatch.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_notify | `EVT_NOTIFY_NEW` (`0x0800`) | (planned) Published when a new notification arrives; the payload is the notification id |
| app_notify | `EVT_NOTIFY_REMOVED` (`0x0801`) | (planned) Published when a single notification is withdrawn |
| app_notify | `EVT_NOTIFY_CLEARED` (`0x0802`) | (planned) Published when all notifications are cleared |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_notify | — | — | — |

## app_ota

BLE DFU / OTA service.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_ota | — | — |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_ota | — | — | — |

## app_phone

Call state.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_phone | `EVT_PHONE_STATE_CHANGED` (`0x0700`) | (planned) Published on state changes such as incoming, answered and hung up |
| app_phone | `EVT_PHONE_RING` (`0x0701`) | (planned) Published once per new incoming call, no payload |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_phone | — | — | — |

## app_display

Display on/off, AOD and brightness.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_display | `EVT_DISPLAY_SLEEP` (`0x0B00`) | (planned) Published on timeout or manual screen-off |
| app_display | `EVT_DISPLAY_WAKE` (`0x0B01`) | (planned) Published on a key press, touch, or wrist-raise wake-up |
| app_display | `EVT_DISPLAY_AOD` (`0x0B02`) | (planned) Published when entering or leaving AOD |
| app_display | `EVT_DISPLAY_BRIGHTNESS` (`0x0B03`) | (planned) Published after a brightness change; the payload is a percentage |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_display | — | — | — |

## app_alarm

Alarms, countdown and hourly chime.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_alarm | `EVT_ALARM_FIRED` (`0x0C00`) | (planned) Published when an alarm is due |
| app_alarm | `EVT_TIMER_DONE` (`0x0C01`) | (planned) Published when the countdown ends |
| app_alarm | `EVT_CHIME` (`0x0C02`) | (planned) Published when the hourly chime triggers |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_alarm | — | — | — |

## app_motion

Gesture and wrist-raise detection.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_motion | `EVT_MOTION_GESTURE` (`0x0D00`) | (planned) Published when gestures such as flip and shake are recognised |
| app_motion | `EVT_MOTION_WRIST_RAISE` (`0x0D01`) | (planned) Published on a wrist-raise wake-up |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_motion | — | — | — |

## app_workout

Workout records and real-time data.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_workout | `EVT_WORKOUT_STATE_CHANGED` (`0x0E00`) | (planned) Published on workout start, pause, resume and stop |
| app_workout | `EVT_WORKOUT_DATA_UPDATED` (`0x0E01`) | (planned) Published when the real-time workout data updates |
| app_workout | `EVT_WORKOUT_GOAL_REACHED` (`0x0E02`) | (planned) Published when the goal is reached |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_workout | — | — | — |

## app_weather

Weather information.

### Published

| Module | Published event | When published |
| --- | --- | --- |
| app_weather | `EVT_WEATHER_UPDATED` (`0x0F00`) | (planned) Published after the phone pushes weather data |

### Subscribed

| Module | Subscribed event | Callback | Purpose |
| --- | --- | --- | --- |
| app_weather | — | — | — |
