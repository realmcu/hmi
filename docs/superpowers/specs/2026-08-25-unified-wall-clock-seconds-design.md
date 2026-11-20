# Unified time basis: wall clock seconds

Date: 2026-08-25
Status: design confirmed, pending implementation
Impact: BLE protocol document, firmware (applications), phone App (HoneyBox)

## 1. Background

The firmware currently has two contradictory interpretations of "what a `uint32_t` time value represents," and the same value is used as two different things in different functions.

The write side treats it as local time: the name of `app_time_set_local()`, the parameter type `app_time_local_t`, and the `app_time.h` comment "Store a validated local calendar value in the hardware RTC" all indicate that the RTC stores a local calendar value. The 32-bit packed value sent by protocol 0x01 contains no timezone field, and by smartwatch convention it is the user's local time.

The read side, however, treats it as UTC: `app_time_now()` directly returns `civil_to_epoch(RTC)`, declared as "Wall-clock time in Unix seconds", and then `app_time_to_local()` and `local_sec_from_utc()` apply `+ s_tz_min * 60` to it (default +480 minutes).

### 1.1 Confirmed existing defects

Since the RTC actually stores local time, that `+8h` is an extra addition:

1. **`EVT_TIME_DAY_CHANGED` fires at local 16:00 instead of midnight.**
   `app_time.c:242-243` adds 28800 seconds to an already-local value, pushing the `local / 86400` rollover point back by 8 hours.
   `health_worker_on_day_changed()` (`health_worker.c:125-130`) therefore clears `s_today_acc` at 4 PM every day, resetting the user's daily step count.
   This is a user-visible functional defect.

2. **The SPORT sync log shows a time offset by +8 hours.**
   `hmi_l2_cmd_sport.c:189` applies a timezone offset again to an already-local `rec.ts_utc`.

3. **`EVT_TIME_TICK_15MIN` is unaffected.** 28800 is an integer multiple of 900, so the extra shift does not change the `% 900` result. This is exactly why the defect went unnoticed for so long: the 15-minute bucketing looks normal, and only day rollover and display are wrong.

4. **The `ts_utc` field name does not match its content.** But because the protocol requires the phone to do no timezone conversion, the final behavior of persistence and reporting is self-consistent and correct -- only the naming lies.

### 1.2 Current state of the protocol document

In `component/protocol/BLE_PROTOCOL_SPEC.html`:

- 0x01 time setting: Value 32 bits = Year(6) + Month(4) + Day(5) + Hour(5) + Minute(6) + Second(6), with no timezone field; the word "UTC" does not appear anywhere.
- Sport/sleep record Timestamp: marked as "Unix timestamp (seconds), consistent with the FlashDB record time".
- The original sport bucket definition treats Timestamp as the end/persistence time; the phone must not shift it back 15 minutes.

Reading the latter two together shows that the document's intent is for the phone to take Timestamp and display it directly as wall clock time. Since the phone does no conversion and the phone sends the user's local calendar value, no UTC actually exists anywhere in the chain -- the system in fact runs on a "local time passed off as Unix seconds" basis; only the naming and comments fail to admit it.

## 2. Goals and Non-goals

**Goal**: make "what basis is this `uint32_t` in" a question that no longer needs asking. The protocol, storage, and firmware internals are unified into a single scalar, and the timezone does not enter the data stream.

**Non-goals**:

- No timezone negotiation. The concept of a timezone does not exist in the firmware (the previously considered "add a timezone negotiation key to the protocol" has been withdrawn).
- We do not pursue continuity of historical data on an absolute timeline when traveling across timezones (see the trade-off in §7).
- No alarm design. The alarm definitions in the protocol are deleted outright (see §4.3), and will be designed separately when `app_alarm` is implemented later.
- No key reporting for factory tests. The 0x21 Timestamp epoch is unified as well (see §4.4), but no implementation is added for that command.

## 3. Core Model

### 3.1 The single scalar

> **Wall clock seconds**: a `uint32_t`, the number of seconds since **1970-01-01 00:00:00**, whose value is "the instant the watch face should display".
> The anchor is defined unilaterally by the phone: the phone takes the local calendar fields and generates the seconds using UTC folding rules.
> **The firmware never adds or subtracts anything after receiving it.** Valid range 1970-2106.

The epoch is fixed at 1970-01-01 00:00:00, i.e. the standard Unix epoch. **The 2000 epoch is not used, so neither side needs any epoch conversion code** (no constants such as `± 946684800` exist). Reasons:

1. Library functions on both sides natively use 1970 as the origin -- newlib's `gmtime_r` `time_t` and Dart's `DateTime.fromMillisecondsSinceEpoch` both do. Any custom epoch would require manually adding/subtracting a constant on every conversion; a single missed addition on one side would silently be off by 30 years, the same class of defect as the "off by 8 hours" this design is meant to eliminate.
2. With a 1970 origin, `uint32_t` remains usable until 2106-02-07, ample for a watch's lifetime. The business and protocol layers use no signed intermediate values and store and use seconds directly; but FlashDB's default `fdb_time_t` is signed 32-bit, so the 64-bit timestamp configuration must be enabled separately to bypass the 2038 boundary. The overflow-saturation logic originally in `app_time.c:156-168` is deleted together with `local_sec_from_utc()`.
3. FlashDB's `fdb_time_t` convention is the Unix epoch. `health_db.c:103` uses the record timestamp directly as the TSDB time key; switching to another epoch would offset the time index from external understanding by 30 years and would also affect the query range of `fdb_tsl_iter_by_time`.

### 3.2 Three corollaries for the firmware side

All are pure arithmetic with no offset:

| Purpose | Expression |
|---|---|
| Day rollover detection | `sec / 86400` |
| Quarter-hour boundary detection | `sec % 900 == 0` |
| Rendering to calendar fields | `gmtime_r(&sec, &tm)` |

`gmtime_r` is the **correct** choice here, not a compromise: the seconds are produced by the phone using UTC rules, so folding back with the same rules is a strict inverse. **Do not use `localtime_r` or `mktime`** -- both read the `TZ` environment variable, and in an embedded environment with `TZ` unset their behavior depends on newlib implementation details, which is exactly the uncertainty this design eliminates.

### 3.3 Concepts that disappear from the firmware

- `s_tz_min` (`app_time.c:60`) -- delete; the timezone concept no longer exists.
- `local_sec_from_utc()` (`app_time.c:162-169`) -- delete; the shift is no longer needed.
- The words `utc` / `local` -- remove from all identifiers and comments, unifying on wall clock seconds.
- `app_time_local_t` (`app_time.h:34-43`) -- **delete**. It is a reinvention of `struct tm`: seven fields corresponding one-to-one to `tm_year`/`tm_mon`/`tm_mday`/`tm_hour`/`tm_min`/`tm_sec`/`tm_wday`, only with different offset conventions (full year vs `-1900`, `1..12` vs `0..11`). Deleting it and using `struct tm` directly is exactly the implementation of "use the C library where possible".

Knock-on effects of deleting `app_time_local_t`:

- `app_time_to_local(uint32_t, app_time_local_t*)` becomes `app_time_to_calendar(uint32_t sec, struct tm *out)`, and the implementation degrades to a single `gmtime_r` call plus a null-pointer check.
- The sole call site `hmi_l2_cmd_sport.c:189` switches to `struct tm`, and the log formatting must change accordingly to `tm_year + 1900` and `tm_mon + 1`. This `+1900` / `+1` conversion is the established cost of using `struct tm`, in exchange for no longer maintaining a parallel type.
- `app_time_set_local(const app_time_local_t*)` becomes `app_time_set(uint32_t sec)`; the parameter is already seconds, so that type no longer appears at all.

### 3.4 On using the C library instead of hand-written implementations

The design intent is to leave as much as possible to libc, but there is one hard constraint: **newlib does not provide `timegm`** (verified against the toolchain `libc.a` symbol table: only `gmtime_r`, `localtime_r`, and `mktime`). And `mktime` depends on `TZ`, so it cannot be used in this design.

Under the new model the write path no longer needs "calendar -> seconds": the phone sends seconds directly, so the bit unpacking of `hmi_l2_decode_time()` and the `days_in_month()` validation are deleted wholesale -- not replaced with libc calls, but with the **requirement gone**.

The read path still needs "calendar -> seconds", because the hardware RTC registers store only calendar fields, and `app_time_now()` must fold the read-back calendar value into seconds. Here we **keep the existing `civil_to_epoch()`** (`app_time.c:117-141`, Howard Hinnant's algorithm): it is verified, fully commented, and does not depend on `TZ`, while libc has no timezone-free equivalent here. Replacing it with `mktime` would reintroduce a timezone dependency, a step backward.

`app_time_set()` needs "seconds -> calendar fields" when writing the RTC, which is the forward direction, so it uses `gmtime_r`. A by-product is that `tm_wday` is provided for free by libc, incidentally fixing the weekday defect recorded in §5.2.

After deleting `app_time_local_t` (§3.3), only two types in the firmware represent calendar fields, each with a clear role: `struct tm` for application-layer rendering, and `posix_rtc_time_t` as the driver interface's hardware representation. Conversion between the two happens in exactly one place inside `app_time.c`.

## 4. Protocol Changes

Changes to `component/protocol/BLE_PROTOCOL_SPEC.html`.
**This is an incompatible (breaking) change: firmware and App must be upgraded to the same version**, and no version gate is introduced for compatibility (see §7).

### 4.1 0x01 time setting -- format change

Value changes from 32-bit packed calendar fields to **4 bytes big-endian wall clock seconds**.
The byte width is unchanged (still 4 bytes), but the semantics are completely different.

The document must state explicitly:

- The seconds are counted from 1970-01-01 00:00:00.
- Its value is "the wall clock instant the device should display".
- The phone is responsible for folding the user's local calendar into seconds; **the device performs no timezone handling**.

In the original packed format, the Year field "since 2000, 6 bits (0~63)" was a product of bit-width compression, not an epoch choice; after switching to 4-byte seconds that constraint disappears.

### 4.2 Upstream Timestamp -- semantics-only change

The Timestamp wire format of sport (0x02) and sleep (0x03) records is unchanged (already 4-byte big-endian seconds). Document wording changes:

- Delete the expression "Unix timestamp (seconds)", which is easily misread as UTC, and change it to "wall clock seconds (counted from 1970-01-01, consistent with the device display)".
- Keep the existing sentence "the phone must not apply any timezone conversion to Timestamp", but turn it into a positive statement: this Timestamp is already the instant that should be displayed, so render it directly. Under the new model this constraint is a natural consequence, no longer an exception that needs explaining.
- The sport record Timestamp is defined as the start of a natural quarter-hour logical bucket, with the statistics interval `[Timestamp, Timestamp + 15 minutes)`. Both complete buckets and partial buckets align to `00/15/30/45`; a partial bucket only means that this logical interval was not fully captured, and the protocol does not express the actual capture start/end times. The phone aggregates dates and time periods directly by Timestamp.

### 4.3 Alarms 0x02/0x04 -- delete definitions

**Remove the definitions of 0x02 (alarm setting), 0x03 (get alarm list request), and 0x04 (get alarm list response) from the protocol document.**

Basis: neither side implements them; they are purely paper definitions. Verified -- the phone side has no alarm code anywhere under `lib/services/`, and the firmware side only has three comments mentioning "the future `app_alarm`" (`app_time.h:20`, `app_time.c:8`, `app_event_defs.h:130`), with no implementation code.

Reason to delete rather than keep: the original 40-bit format carried Day flags (weekly repeat rules), the only place in the protocol that uses calendar fields to represent time. Keeping it would require maintaining a "everything uses seconds, except alarms" note in the document long-term, and that exception serves a feature that does not yet exist. After deletion **there are no calendar fields left in the protocol; "everything uses seconds" has zero exceptions**.

When `app_alarm` is implemented in the future it will be redesigned: the trigger condition of a repeating alarm is essentially "time-of-day + weekday mask", which is a different thing from "the seconds of a certain instant", and will be defined according to actual needs at that time, unconstrained by this design.

Key slots 0x02/0x03/0x04 are considered reserved and not reassigned, to avoid confusion with historical implementations. On the firmware side, the three macros `HMI_L2_SET_ALARM`, `HMI_L2_GET_ALARM_REQ`, and `HMI_L2_GET_ALARM_RSP` in `hmi_l2.h` are deleted as well (no references).

### 4.4 0x21 key test Timestamp -- change to the 1970 epoch

Timestamp changes from "seconds since 2000" to **wall clock seconds (1970 epoch)**, consistent with the definition in §3.1. The byte width is unchanged (still 4-byte big-endian).

Basis: neither side implements it. Verified -- the handler for the firmware `hmi_l2_cmd_factory.c` only prints the key value with a single `PROTO_LOG` and parses no payload; the phone side has no factory command code. What changes is a purely paper definition, **with zero implementation cost**.

Reason it must change rather than be kept: keeping it would require maintaining a "the only non-1970-epoch exception in the whole protocol" warning in the document long-term, and that exception serves a feature that does not exist -- exactly the same logic as deleting the alarms in §4.3. And it is more dangerous than the alarms: a 4-byte timestamp field whose epoch differs from the rest of the protocol by 30 years; anyone implementing factory tests in the future and following the style of the other commands will get it wrong, and a 30-year deviation may not be immediately exposed in a factory environment.

At this point **all time fields in the protocol are unified to 1970-epoch wall clock seconds, with zero exceptions**.

## 5. Firmware Changes

### 5.1 Change list

| File | Change |
|---|---|
| `app/app_time/app_time.h` | Delete `app_time_local_t` (use `struct tm` instead); `app_time_set_local(const app_time_local_t*)` -> `app_time_set(uint32_t sec)`; `app_time_to_local()` -> `app_time_to_calendar(uint32_t, struct tm*)`; change the `app_time_now()` comment to wall clock seconds; add `#include <time.h>`; remove the alarm references from the "Alarms / calendar do not live here" file header or make it neutral |
| `app/app_time/app_time.c` | Delete `s_tz_min`, `local_sec_from_utc()`; keep `civil_to_epoch()` for `app_time_now()` to read the RTC; `app_time_set()` uses `gmtime_r` to expand seconds into `posix_rtc_time_t` (`wday` taken from `tm_wday`); `app_time_to_calendar()` degrades to a single `gmtime_r`; the day index and quarter-hour checks in `rtc_second_cb()` use the seconds value directly, with the shift removed; update the file header comment and delete the "Local means UTC + s_tz_min" paragraph |
| `app/app_core/app_event_defs.h` | `app_evt_time_synced_t` changes from six calendar fields to a single `uint32_t sec`; update comments |
| `app/app_protocol/hmi_l2_cmd_settings.c` | Delete the bit unpacking of `hmi_l2_decode_time()` and `days_in_month()`; read a 4-byte big-endian seconds value instead; call `app_time_set()`; the `EVT_TIME_SYNCED` payload becomes seconds |
| `app/app_health/app_health_internal.h` | `health_pedo_record_t.ts_utc` -> `ts`; unify comments to wall clock seconds, and clarify sport records as the start of a natural quarter-hour bucket |
| `app/app_health/health_worker.c` | `ts_utc` -> `ts`; `boundary_utc` -> `boundary_sec` |
| `app/app_health/health_db.c` | `ts_utc` -> `ts` (10 places in total); rename the `health_db_save_synced_ts()` parameter |
| `app/app_protocol/hmi_l2_cmd_sport.c` | Rename the `sport_encode_record()` field; change the `app_time_to_local` call to `app_time_to_calendar`, and change the log formatting to `tm_year + 1900` / `tm_mon + 1` |
| `component/protocol/hmi_l2.h` | Delete `HMI_L2_SET_ALARM`, `HMI_L2_GET_ALARM_REQ`, `HMI_L2_GET_ALARM_RSP` (§4.3, no references) |

`ts_utc` -> `ts` is a pure rename: the field type, offset, and struct layout are all unchanged, and **existing records in Flash are unaffected; no data migration is needed**.

### 5.2 Incidental fix: the weekday lie

`hmi_l2_cmd_settings.c:54` currently fills `weekday` with 0 and writes it to the RTC, while the convention in `posix_ioctl_rtc.h` is "set `0xFF` when the driver cannot fill it"; 0 means Sunday -- i.e. the current implementation lies to the RTC that it is Sunday. If the Realtek port writes wday into hardware registers, or if some consumer reads the RTC's wday field in the future, it would get a wrong value.

In the new implementation `app_time_set()` uses `gmtime_r` to expand the seconds, `tm_wday` is computed by libc, and it is written directly into `posix_rtc_time_t.wday`, so the defect disappears naturally -- this is also the incidental benefit of deleting `app_time_local_t` and using `struct tm`: the original type required the caller to fill in `weekday` themselves, whereas libc's `struct tm` has it computed.

### 5.3 What does not change

- The event IDs of `EVT_TIME_TICK_15MIN` / `EVT_TIME_DAY_CHANGED` and the `uint32_t` payload type are unchanged; only the semantics are clarified as wall clock seconds.
- The mechanism by which the RTC 1 Hz interrupt drives boundary events is unchanged.
- ISR discipline is unchanged: `rtc_second_cb()` still only does the GET_TIME ioctl and `app_event_publish_isr()`.
- The business record layout of `health_pedo_record_t` is unchanged; however `FDB_USING_TIMESTAMP_64BIT` must be enabled, so the sector header and log index layout of the FlashDB TSDB change from 32-bit timestamps to 64-bit timestamps. On upgrade the pedo TSDB must be rebuilt; old indexes cannot be read directly with the new layout.

## 6. Phone-side Changes (HoneyBox)

### 6.1 `lib/services/watch_time_protocol.dart`

`buildSetTime(DateTime localTime)` changes to send 4-byte big-endian seconds:

```dart
final seconds = DateTime.utc(
  localTime.year, localTime.month, localTime.day,
  localTime.hour, localTime.minute, localTime.second,
).millisecondsSinceEpoch ~/ 1000;
```

**Key point: you must use `DateTime.utc(...)`, not `DateTime(...)`.**
The latter applies the device timezone offset a second time, producing wrong seconds.
What is taken here is the **calendar fields** of `localTime`, folded with UTC rules, which is exactly the anchor-generation method defined in §3.1.

Delete the packed bit operations. The `minimumYear`/`maximumYear` validation may be kept as a sanity check, but it is no longer a format constraint.

The call site `lib/providers/watch_bind_provider.dart:141` (`WatchTimeProtocol.buildSetTime(_clock())`, where `_clock` defaults to `DateTime.now`) needs no change.

### 6.2 `lib/services/watch_health_protocol.dart`

`_readTimestamp()` (lines 301-307) currently implements:

```dart
return DateTime.fromMillisecondsSinceEpoch(seconds * 1000, isUtc: true)
    .toLocal();
```

**Remove `.toLocal()`**, keeping `isUtc: true`. The seconds are already the instant that should be displayed, so calling `toLocal()` again would apply the timezone offset a second time. Also update the line 300 comment "Records are UTC on the wire".

### 6.3 `lib/pages/watch/health/watch_health_data.dart` -- must be checked together

After removing `.toLocal()`, `record.timestamp` becomes a UTC-flagged `DateTime`. Dart's `.hour`/`.year` and other accessors return UTC fields for UTC-flagged objects and local fields for local objects -- so the record side is automatically correct, but **places that compare against a local `DateTime` will be wrong**.

Places that need checking and fixing:

- Line 103 `_today`: constructed from `syncedAt`, and `syncedAt` comes from `_clock()` (the local `DateTime.now`) at `watch_health_repository.dart:142`. `_today` must be changed to UTC-flagged to compare correctly against records.
- Lines 106, 235 `_sameDate(record.timestamp, ...)`
- Lines 209-210, the time-period filtering on `record.timestamp.hour`
- Line 230 `_today.subtract(...)`
- Line 258, the `_sameDate()` implementation itself

Fix principle: make both sides participating in the comparison UTC-flagged, i.e. construct the `syncedAt` side on the wall clock basis as well.
Check each place during implementation; do not do a bulk replacement.

### 6.4 Display path

`watch_health_page.dart:569` `_recordTime()` and the `_date()`/`_time()` on lines 78/124 format `DateTime` fields directly; on UTC-flagged objects they output the correct wall clock time and need no change -- but confirm by actual testing that they do not go through `toLocal()`.

## 7. Trade-offs and Accepted Costs

**Discontinuity when traveling across timezones.** When the phone sends wall clock seconds for a new timezone, the seconds jump; historical records overlap or have gaps at the jump point, and records from different timezone segments cannot be compared on the same absolute timeline.

Reason for accepting this cost: each record always equals "the time the watch face showed at that moment" and is always interpretable; whereas the alternative (storing true UTC), given that the protocol carries no timezone, would have to guess the timezone hard-coded in the firmware to derive UTC -- and once the guess is wrong, the error is permanently baked into Flash and cannot be recovered afterward. This approach never needs to know the timezone, so it can never guess wrong.

**Breaking change.** Mixing old and new firmware/phone silently shifts by one timezone with no error. Confirmed acceptable: we are currently in the development phase, devices can be reflashed at any time, and both sides are released in sync. The document records this incompatibility. The protocol introduces no version gate (the SPORT command already has a version=1 precedent, but it is not used this time).

**"Everything uses seconds" with zero exceptions.** All time fields in the protocol are unified to 1970-epoch wall clock seconds: the alarm definitions are deleted (§4.3) and the 0x21 key test changes to the 1970 epoch (§4.4). The document does not need to keep any "but there is one exception" note.

## 8. Verification

### 8.1 Round-trip consistency tests (one per side)

The core invariant: **the seconds produced by the phone, after being expanded by the firmware's `gmtime_r`, must reproduce the phone's original local calendar fields.**

- Phone side: extend `test/services/watch_time_protocol_test.dart` to assert that the 4-byte seconds generated from a given local time match the expected constant values.
- Firmware side: assert that the seconds, after `gmtime_r`, yield the same year/month/day/hour/minute/second.
- Phone side: extend `test/services/watch_health_protocol_test.dart` to assert that the `DateTime` fields returned by `_readTimestamp()` for given seconds match the expected wall clock time and do not change with the runtime environment's timezone.

The last one is especially important: the tests must pass under any `TZ`, which is exactly the executable definition of "the timezone does not enter the data stream".

### 8.2 Defect regression confirmation

- **Day rollover instant**: verify that `EVT_TIME_DAY_CHANGED` fires at wall clock 00:00 instead of 16:00 (§1.1 defect 1).
- **Sync log**: verify that the time printed by the SPORT sync log matches the watch face and is no longer offset by 8 hours (§1.1 defect 2) -- this is the cheapest end-to-end verification point.
- **Quarter-hour boundary**: confirm that the `EVT_TIME_TICK_15MIN` behavior has not regressed.

### 8.3 Compatibility with existing data

`ts_utc` -> `ts` is itself a pure rename; but after the sport record Timestamp changes from a closing boundary to a logical bucket start, old and new records cannot be interpreted together. In addition, the default `fdb_time_t` is signed 32-bit, and values after 2038 become negative. The firmware must enable `FDB_USING_TIMESTAMP_64BIT`, which changes the sector header and log index layout of FlashDB, so old TSDB partitions cannot be made compatible in place.

The current development phase adopts a one-time rebuild strategy: the KVDB stores the pedo layout version; when either the 64-bit timestamp layout or the sport bucket Timestamp semantics change, the version is bumped, and the first boot of the new firmware version only erases `fdb_tsdb1`, resets the `health.synced` watermark to zero, and finally writes the new layout version. Other KVDB configuration and BF data are unaffected. The marker is written last, so a power loss during migration will safely retry on the next boot. If preserving historical data is required after mass production, an offline export/import should be implemented separately, rather than relying on FlashDB to automatically recognize old layouts.
