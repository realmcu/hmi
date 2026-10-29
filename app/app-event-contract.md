# 蓝牙smartwatch app 层事件契约

> （规划）为已定义或建议的事件 ID，代码尚未实现。

## app_time

时钟 / RTC / 校时。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_time | `EVT_TIME_TICK_15MIN` (`0x0302`) | RTC 秒中断，墙钟 :00/:15/:30/:45 触发；payload 为边界时刻墙钟秒 |
| app_time | `EVT_TIME_DAY_CHANGED` (`0x0303`) | RTC 秒中断，墙钟 00:00:00 触发；午夜先于 15MIN 发布 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_time | — | — | — |

## app_power

电量与充电状态。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_power | `EVT_POWER_LEVEL` (`0x0200`) | （规划）电量百分比变化时发布 |
| app_power | `EVT_POWER_LOW` (`0x0201`) | （规划）跌破低电阈值时发布，边沿触发 |
| app_power | `EVT_POWER_CHARGING` (`0x0202`) | （规划）充放电状态变化时发布 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_power | — | — | — |

## app_setting

设置项的 KV 语义层。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_setting | `EVT_SETTING_CHANGED` (`0x0400`) | （规划）设置项写入成功后发布，载荷是 key id |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_setting | — | — | — |

## app_bluetooth

BLE 与 BR/EDR 接入。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_bluetooth | `EVT_BLE_CONNECTED` (`0x0100`) | BLE 连接建立后由连接回调发布 |
| app_bluetooth | `EVT_BLE_DISCONNECTED` (`0x0101`) | BLE 断开时由连接回调发布 |
| app_bluetooth | `EVT_BLE_CONN_PARAM` (`0x0102`) | 每次连接参数更新都会发布，断开时不发布 |
| app_bluetooth | `EVT_BLE_PAIRING_REQ` (`0x0103`) | （规划）配对请求时发布，UI 显示配对码 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_bluetooth | — | — | — |

## app_protocol

L2 协议命令处理（hmi_l2）。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_protocol | `EVT_TIME_SYNCED` (`0x0300`) | 手机下发 HMI_L2_SET_TIME，`app_time_set()` 写 RTC 成功后发布 |
| app_protocol | `EVT_USER_BOUND` (`0x0A00`) | 绑定握手成功后发布，无载荷 |
| app_protocol | `EVT_USER_UNBOUND` (`0x0A01`) | 收到 HMI_L2_UNBIND 后发布，无载荷 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_protocol | `EVT_BLE_CONN_PARAM` (`0x0102`) | `on_ble_conn_param` | 缓存最新连接参数，供 L2 查询应答 |
| app_protocol | `EVT_BLE_DISCONNECTED` (`0x0101`) | `on_ble_disconnected`（conn_param） | 清空缓存的连接参数 |
| app_protocol | `EVT_BLE_DISCONNECTED` (`0x0101`) | `on_ble_disconnected`（sport） | 丢弃运动同步会话 |
| app_protocol | `EVT_BLE_DISCONNECTED` (`0x0101`) | `on_ble_disconnected`（xfer） | 中止文件传输并复位 |

## app_health

计步采集、落库与当日汇总。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_health | `EVT_HEALTH_STEPS_UPDATED` (`0x0500`) | 计步 worker 成功写入 TSDB 后发布当日总步数 |
| app_health | `EVT_HEALTH_HR_UPDATED` (`0x0501`) | （规划）心率测量更新时发布 |
| app_health | `EVT_HEALTH_SPO2_UPDATED` (`0x0502`) | （规划）血氧测量更新时发布 |
| app_health | `EVT_HEALTH_SLEEP_UPDATED` (`0x0503`) | （规划）睡眠数据更新时发布 |
| app_health | `EVT_HEALTH_BP_UPDATED` (`0x0504`) | （规划）血压测量更新时发布 |
| app_health | `EVT_HEALTH_STRESS_UPDATED` (`0x0505`) | （规划）压力测量更新时发布 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_health | `EVT_TIME_SYNCED` (`0x0300`) | `on_evt_time_synced` | 记录校时完成，尝试启动 worker |
| app_health | `EVT_USER_BOUND` (`0x0A00`) | `on_evt_user_bound` | 记录已绑定，尝试启动 worker |
| app_health | `EVT_USER_UNBOUND` (`0x0A01`) | `on_evt_user_unbound` | 停止 worker，清空当日数据 |
| app_health | `EVT_POWER_LOW` (`0x0201`) | `on_evt_power_low` | 先 flush 再停止 worker |
| app_health | `EVT_TIME_TICK_15MIN` (`0x0302`) | `on_evt_time_tick_15min` | 按 payload 的边界时间推进 15 分钟桶 |
| app_health | `EVT_TIME_DAY_CHANGED` (`0x0303`) | `on_evt_time_day_changed` | 清零当日累计 |

## app_media

A2DP / AVRCP 媒体控制。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_media | `EVT_MEDIA_STATE_CHANGED` (`0x0600`) | （规划）播放状态变化时发布 |
| app_media | `EVT_MEDIA_METADATA` (`0x0601`) | （规划）曲目信息变化时发布，无载荷 |
| app_media | `EVT_MEDIA_VOLUME_CHANGED` (`0x0602`) | （规划）音量变化时发布 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_media | — | — | — |

## app_notify

通知缓存与分发。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_notify | `EVT_NOTIFY_NEW` (`0x0800`) | （规划）收到新通知时发布，载荷为通知 id |
| app_notify | `EVT_NOTIFY_REMOVED` (`0x0801`) | （规划）单条通知被撤销时发布 |
| app_notify | `EVT_NOTIFY_CLEARED` (`0x0802`) | （规划）清空全部通知时发布 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_notify | — | — | — |

## app_ota

BLE DFU / OTA 服务。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_ota | — | — |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_ota | — | — | — |

## app_phone

通话状态。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_phone | `EVT_PHONE_STATE_CHANGED` (`0x0700`) | （规划）来电、接通、挂断等状态变化时发布 |
| app_phone | `EVT_PHONE_RING` (`0x0701`) | （规划）每通新来电发布一次，无载荷 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_phone | — | — | — |

## app_display

屏幕亮灭、AOD 与亮度。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_display | `EVT_DISPLAY_SLEEP` (`0x0B00`) | （规划）超时或手动息屏时发布 |
| app_display | `EVT_DISPLAY_WAKE` (`0x0B01`) | （规划）按键、触摸或抬腕亮屏时发布 |
| app_display | `EVT_DISPLAY_AOD` (`0x0B02`) | （规划）进入或退出 AOD 时发布 |
| app_display | `EVT_DISPLAY_BRIGHTNESS` (`0x0B03`) | （规划）亮度调整后发布，载荷为百分比 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_display | — | — | — |

## app_alarm

闹钟、倒计时与整点报时。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_alarm | `EVT_ALARM_FIRED` (`0x0C00`) | （规划）闹钟到点时发布 |
| app_alarm | `EVT_TIMER_DONE` (`0x0C01`) | （规划）倒计时结束时发布 |
| app_alarm | `EVT_CHIME` (`0x0C02`) | （规划）整点报时触发时发布 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_alarm | — | — | — |

## app_motion

手势与抬腕识别。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_motion | `EVT_MOTION_GESTURE` (`0x0D00`) | （规划）识别到翻转、摇动等手势时发布 |
| app_motion | `EVT_MOTION_WRIST_RAISE` (`0x0D01`) | （规划）抬腕唤醒时发布 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_motion | — | — | — |

## app_workout

运动记录与实时数据。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_workout | `EVT_WORKOUT_STATE_CHANGED` (`0x0E00`) | （规划）运动开始、暂停、继续、结束时发布 |
| app_workout | `EVT_WORKOUT_DATA_UPDATED` (`0x0E01`) | （规划）运动中的实时数据更新时发布 |
| app_workout | `EVT_WORKOUT_GOAL_REACHED` (`0x0E02`) | （规划）达到目标时发布 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_workout | — | — | — |

## app_weather

天气信息。

### 发布

| 模块 | 发布事件 | 发布时机 |
| --- | --- | --- |
| app_weather | `EVT_WEATHER_UPDATED` (`0x0F00`) | （规划）手机推送天气后发布 |

### 订阅

| 模块 | 订阅事件 | 回调 | 作用 |
| --- | --- | --- | --- |
| app_weather | — | — | — |
