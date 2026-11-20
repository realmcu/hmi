# Unified time semantics: wall clock seconds

Date: 2026-08-25
Status: design confirmed, implementation pending
Scope: BLE protocol document, firmware (applications), phone App (HoneyBox)

## 1. Background

The firmware currently holds two contradictory interpretations of "what a single `uint32_t`
time value means"; the same value is used as two different things in different functions.

The write side treats it as local time: the naming of `app_time_set_local()`, the parameter type
`app_time_local_t`, and the comment in `app_time.h` "Store a validated local
calendar value in the hardware RTC", all indicate that the RTC stores a local calendar value.
The 32-bit packed value sent by protocol 0x01 carries no timezone field; by watch industry
convention it is the user's local time.

The read side, however, treats it as UTC: `app_time_now()` returns
`civil_to_epoch(RTC)` directly, declared as "Wall-clock time in Unix seconds",
and then `app_time_to_local()` and `local_sec_from_utc()` apply
`+ s_tz_min * 60` to it (default +480 minutes).

### 1.1 Confirmed existing defects

Since the RTC actually stores local time, that `+8h` is added spuriously:

1. **`EVT_TIME_DAY_CHANGED` fires at 16:00 local, not at midnight.**
   `app_time.c:242-243` adds another 28800 seconds to a value that is already local,
   pushing the `local / 86400` rollover point back by 8 hours.
   `health_worker_on_day_changed()` (`health_worker.c:125-130`)
   therefore zeroes `s_today_acc` at 4 p.m. every day, resetting the user's step count for the day.
   This is a user-visible functional defect.

2. **SPORT sync log timestamps are shifted by +8 hours.**
   `hmi_l2_cmd_sport.c:189` applies a timezone offset to `rec.ts_utc`,
   which is already local.

3. **`EVT_TIME_TICK_15MIN` is unaffected.** 28800 is an integer multiple of 900, so
   the extra shift does not change the result of `% 900`. This is exactly why the defect went
   unnoticed for so long: 15-minute bucketing looks right, only the day rollover and the
   display are wrong.

4. **The `ts_utc` field name does not match its content.** But since the protocol requires the
   phone not to do timezone conversion, the resulting storage and reporting behaviour is
   self-consistently correct —— only the name lies.

### 1.2 Current state of the protocol document

In `component/protocol/BLE_PROTOCOL_SPEC.html`:

- 0x01 time set: Value 32 bits = Year(6) + Month(4) + Day(5) +
  Hour(5) + Minute(6) + Second(6), no timezone field, and "UTC" never appears.
- Sport/sleep record Timestamp: marked as "Unix timestamp (seconds), consistent with the FlashDB
  record time".
- The original sport bucket definition treated Timestamp as the end/storage time; the phone must not move it back by 15 minutes.

Reading the last two together shows the document intends the phone to take Timestamp and display
it directly as wall time. Since the phone does not convert, and what the phone sends is the
user's local calendar value, no UTC actually exists anywhere along the chain —— the system in
fact runs on "local time masquerading as Unix seconds", and only the names and comments fail to
admit it.

## 2. Goals and non-goals

**Goal**: make "what semantics does this `uint32_t` carry" a question nobody has to ask.
Protocol, storage and firmware internals are unified on a single scalar, and the timezone does
not enter the data flow.

**Non-goals**:

- No timezone negotiation. The firmware has no concept of a timezone (the previously considered
  "add a timezone negotiation key to the protocol" has been dropped).
- No continuity of historical data on an absolute timeline when travelling across timezones (see the trade-off in §7).
- No alarm design. The alarm definitions in the protocol are deleted outright (see §4.3);
  `app_alarm` will be designed separately when it is implemented.
- No key-press reporting for factory test. The 0x21 Timestamp epoch is unified as well
  (see §4.4), but no implementation is added for that command.

## 3. Core model

### 3.1 The single scalar

> **Wall clock seconds**: a `uint32_t`, the number of seconds since
> **1970-01-01 00:00:00**, whose value is "the instant the watch face should display".
> The anchor is defined by the phone alone: the phone takes the local calendar fields and
> generates the seconds using UTC conversion rules.
> **The firmware never adds or subtracts anything after receiving it.** Valid range 1970–2106.

The epoch is fixed at 1970-01-01 00:00:00, i.e. the standard Unix epoch. **No year-2000 epoch is
used, so neither side needs any epoch conversion code** (there is no constant such as
`± 946684800`). Reasons:

1. Library functions on both sides natively use 1970 as the origin —— newlib `gmtime_r`'s `time_t`,
   and Dart's `DateTime.fromMillisecondsSinceEpoch`. Any custom epoch
   would require manual add/subtract of a constant on every conversion; missing it on one side
   silently shifts by 30 years, a defect of the same kind as the "8 hour shift" this design
   eliminates.
2. A `uint32_t` counted from 1970 is usable until 2106-02-07, ample for a watch's lifetime.
   Business and protocol layers use no signed intermediates; seconds are stored and used as-is. FlashDB's default
   `fdb_time_t`, however, is signed 32-bit, so the 64-bit timestamp configuration must be enabled
   separately to bypass the 2038 boundary. The overflow saturation logic originally in
   `app_time.c:156-168` is deleted together with `local_sec_from_utc()`.
3. FlashDB's `fdb_time_t` convention is the Unix epoch. `health_db.c:103`
   uses the record timestamp directly as the TSDB time key; switching to another epoch would put
   the time index 30 years out of step with external expectations and would drag in the query
   ranges of `fdb_tsl_iter_by_time`.

### 3.2 Three corollaries on the firmware side

All pure arithmetic, containing no offsets:

| Use | Expression |
|---|---|
| Day rollover detection | `sec / 86400` |
| Quarter-hour boundary detection | `sec % 900 == 0` |
| Render as calendar fields | `gmtime_r(&sec, &tm)` |

`gmtime_r` is the **correct** choice here, not a compromise: the seconds were produced by the
phone using UTC rules, so converting back with the same rules is an exact inverse. **`localtime_r`
or `mktime` must not be used** —— both read the `TZ` environment variable, and with `TZ` unset in
an embedded environment their behaviour depends on newlib implementation details, which is
exactly the uncertainty this design removes.

### 3.3 Concepts that disappear from the firmware

- `s_tz_min` (`app_time.c:60`) —— deleted; the concept of a timezone no longer exists.
- `local_sec_from_utc()` (`app_time.c:162-169`) —— deleted; no shift is needed any more.
- the words `utc` / `local` —— removed from all identifiers and comments,
  unified as wall clock seconds.
- `app_time_local_t` (`app_time.h:34-43`) —— **deleted**. It is a
  reinvention of `struct tm`: its seven fields map one-to-one onto `tm_year`/`tm_mon`/
  `tm_mday`/`tm_hour`/`tm_min`/`tm_sec`/`tm_wday`, only with different offset conventions
  (full year vs `-1900`, `1..12` vs `0..11`). Deleting it and using
  `struct tm` directly is precisely "use the C library wherever possible" put into practice.

Knock-on effects of deleting `app_time_local_t`:

- `app_time_to_local(uint32_t, app_time_local_t*)` →
  `app_time_to_calendar(uint32_t sec, struct tm *out)`,
  whose implementation degenerates to a single `gmtime_r` call plus a null check.
- the only call site, `hmi_l2_cmd_sport.c:189`, switches to `struct tm`,
  and the log formatting there must become `tm_year + 1900` / `tm_mon + 1`.
  That `+1900` / `+1` conversion is the accepted price of using `struct tm`,
  paid in exchange for no longer maintaining a parallel type.
- `app_time_set_local(const app_time_local_t*)` →
  `app_time_set(uint32_t sec)`; the parameter is already seconds, so that type no longer appears at all.

### 3.4 On using the C library instead of hand-written implementations

The design intent is to delegate to libc as far as possible, but there is one hard constraint:
**newlib does not provide `timegm`** (verified against the toolchain `libc.a` symbol table: only
`gmtime_r`, `localtime_r` and `mktime` exist). And `mktime` depends on `TZ`, so it cannot be used
in this design.

Under the new model the write path no longer needs "calendar → seconds": the phone sends the
seconds directly, so the bit unpacking in `hmi_l2_decode_time()` and the whole of the
`days_in_month()` validation are deleted —— not replaced by libc calls, but **no longer needed**.

The read path still needs "calendar → seconds", because the hardware RTC registers only hold
calendar fields, so `app_time_now()` must convert the calendar value read back into seconds. Here
the **existing `civil_to_epoch()` is kept** (`app_time.c:117-141`, Howard Hinnant's algorithm):
it is verified, fully commented, does not depend on `TZ`, and libc has no equivalent without a
timezone. Replacing it with `mktime` would reintroduce the timezone dependency, a step backwards.

`app_time_set()` needs "seconds → calendar fields" when writing the RTC, the forward direction,
and uses `gmtime_r`. A side benefit is that `tm_wday` comes for free from libc, which
incidentally fixes the weekday defect recorded in §5.2.

After deleting `app_time_local_t` (§3.3) only two types in the firmware represent calendar
fields, each with a clear owner: `struct tm` for application-layer rendering, and
`posix_rtc_time_t` as the driver interface's hardware representation. Conversion between them
happens in exactly one place, inside `app_time.c`.

## 4. Protocol changes

Changes to `component/protocol/BLE_PROTOCOL_SPEC.html`.
**This is an incompatible (breaking) change: firmware and App must upgrade together**;
no version gate is introduced for compatibility (see §7).

### 4.1 0x01 time set —— format change

Value changes from 32-bit packed calendar fields to **4 bytes big-endian wall clock seconds**.
The byte width is unchanged (still 4 bytes); the semantics are completely different.

The document must state explicitly:

- The seconds are counted from 1970-01-01 00:00:00.
- The value is "the wall clock instant the device should display".
- The phone is responsible for converting the user's local calendar into seconds; **the device does no timezone handling whatsoever**.

The Year field in the original packed format being "from year 2000, 6 bits (0~63)" was a
by-product of bit-width compression, not an epoch choice; once it becomes 4 bytes of seconds that
constraint disappears.

### 4.2 Uplink Timestamp —— semantics only

The wire format of the Timestamp in sport (0x02) and sleep (0x03) records is unchanged
(it is already 4 bytes big-endian seconds). The wording in the document changes:

- Delete "Unix timestamp (seconds)", which is easily misread as UTC, and change it to
  "wall clock seconds (from 1970-01-01, consistent with the device display)".
- The existing sentence "the phone must not timezone-convert the Timestamp again" is kept, but
  restated positively: the Timestamp already is the instant to display, so simply render it. Under the new model this constraint
  is a natural consequence, no longer an exception that needs explaining.
- The sport record Timestamp is defined as the start of a natural quarter-hour logical bucket, with the statistical interval
  `[Timestamp, Timestamp + 15 minutes)`. Full buckets and partial buckets are both aligned to
  `00/15/30/45`; "partial" only means that the logical interval was not fully captured, and the protocol does not express the
  actual acquisition start/end times. The phone derives dates and periods directly from the Timestamp.

### 4.3 Alarms 0x02/0x04 —— definitions deleted

**Remove the definitions of 0x02 (set alarm), 0x03 (get alarm list request) and
0x04 (get alarm list response) from the protocol document.**

Rationale: neither side implements them; they are a paper-only definition. Verified —— the phone side has no
alarm code at all under `lib/services/`; the firmware side mentions "the future `app_alarm`" in only three comments
(`app_time.h:20`, `app_time.c:8`, `app_event_defs.h:130`),
with no implementation code.

Why delete rather than keep: the original 40-bit format carried Day flags (weekly repeat rules),
the only place in the protocol that used calendar fields to express time. Keeping it would mean maintaining a note in the document
saying "everything uses seconds, except alarms", and that exception would serve a feature that does not yet
exist. After deletion **there are no calendar fields left anywhere in the protocol, and "everything uses seconds" holds with zero exceptions**.

`app_alarm` will be designed afresh when it is implemented: a repeating alarm's trigger condition
is essentially "time of day + weekday mask", a different thing from "seconds at a given instant";
it will be defined per actual requirements at that time, unconstrained by this design.

Keys 0x02/0x03/0x04 are treated as reserved and no longer assigned, to avoid confusion with historical implementations.
On the firmware side the `HMI_L2_SET_ALARM`, `HMI_L2_GET_ALARM_REQ` and
`HMI_L2_GET_ALARM_RSP` macros in `hmi_l2.h` are deleted as well (there are no references).

### 4.4 0x21 key test Timestamp —— switched to the 1970 epoch

The Timestamp changes from "seconds since year 2000" to **wall clock seconds (1970 epoch)**,
consistent with the definition in §3.1. The byte width is unchanged (still 4 bytes big-endian).

Rationale: neither side implements it. Verified —— the handler in firmware `hmi_l2_cmd_factory.c`
has just one line of `PROTO_LOG` printing the key value and parses no payload; the phone side has no factory
command code. What changes is a paper-only definition, so the **implementation cost is zero**.

Why it must change rather than stay: keeping it would mean maintaining a long-term warning in the document about
"the only non-1970-epoch exception in the whole protocol", and that exception would serve a feature that does not
exist —— exactly the same logic as deleting the alarms in §4.3. And it is more dangerous than the alarms: a
4-byte timestamp field whose epoch differs from the rest of the protocol by 30 years; whoever implements factory test later
following the pattern of the other commands will get it wrong, and a 30-year error may not be immediately obvious in a factory environment.

With this, **every time field in the protocol is unified as 1970-epoch wall clock seconds, with zero exceptions**.

## 5. Firmware changes

### 5.1 Change list

| File | Change |
|---|---|
| `app/app_time/app_time.h` | Delete `app_time_local_t` (use `struct tm` instead); `app_time_set_local(const app_time_local_t*)` → `app_time_set(uint32_t sec)`; `app_time_to_local()` → `app_time_to_calendar(uint32_t, struct tm*)`; change the `app_time_now()` comment to wall clock seconds; add `#include <time.h>`; remove the alarm reference from the header line "Alarms / calendar do not live here" or make it neutral |
| `app/app_time/app_time.c` | Delete `s_tz_min` and `local_sec_from_utc()`; keep `civil_to_epoch()` for `app_time_now()` reading the RTC; `app_time_set()` expands seconds into `posix_rtc_time_t` with `gmtime_r` (`wday` taken from `tm_wday`); `app_time_to_calendar()` degenerates to a single `gmtime_r`; in `rtc_second_cb()` use the second value directly for the day index and quarter-hour checks, dropping the shift; update the file header comment and delete the "Local means UTC + s_tz_min" paragraph |
| `app/app_core/app_event_defs.h` | `app_evt_time_synced_t` changes from six calendar fields to a single `uint32_t sec`; update the comment |
| `app/app_protocol/hmi_l2_cmd_settings.c` | Delete the bit unpacking in `hmi_l2_decode_time()` and `days_in_month()`; read 4 bytes of big-endian seconds instead; call `app_time_set()`; the `EVT_TIME_SYNCED` payload becomes seconds |
| `app/app_health/app_health_internal.h` | `health_pedo_record_t.ts_utc` → `ts`; comments unified as wall clock seconds, and sport records further clarified as natural quarter-hour bucket starts |
| `app/app_health/health_worker.c` | `ts_utc` → `ts`; `boundary_utc` → `boundary_sec` |
| `app/app_health/health_db.c` | `ts_utc` → `ts` (10 occurrences in total); rename the `health_db_save_synced_ts()` parameter |
| `app/app_protocol/hmi_l2_cmd_sport.c` | Rename the `sport_encode_record()` fields; change the `app_time_to_local` call to `app_time_to_calendar` and the log formatting to `tm_year + 1900` / `tm_mon + 1` |
| `component/protocol/hmi_l2.h` | Delete `HMI_L2_SET_ALARM`, `HMI_L2_GET_ALARM_REQ`, `HMI_L2_GET_ALARM_RSP` (§4.3, no references) |

`ts_utc` → `ts` is a pure rename: field type, offset and struct layout are all unchanged,
so **records already stored in Flash are unaffected and no data migration is needed**.

### 5.2 Incidental fix: the weekday lie

`hmi_l2_cmd_settings.c:54` currently fills `weekday` with 0 and writes it to the RTC,
while the convention in `posix_ioctl_rtc.h` is "the driver sets `0xFF` when it cannot fill it", and
0 means Sunday —— i.e. the current implementation tells the RTC it is Sunday. If the Realtek port writes wday
into a hardware register, or if a consumer reads the RTC's wday field in future, it will get a wrong value.

In the new implementation `app_time_set()` expands the seconds with `gmtime_r`, `tm_wday` is computed
by libc and filled straight into `posix_rtc_time_t.wday`, and the defect disappears naturally —— another incidental benefit of
deleting `app_time_local_t` and switching to `struct tm`:
the old type required the caller to fill in `weekday` itself, whereas libc's `struct tm` comes pre-computed.

### 5.3 What does not change

- The event IDs of `EVT_TIME_TICK_15MIN` / `EVT_TIME_DAY_CHANGED` and the
  `uint32_t` payload type are unchanged; only the semantics are clarified as wall clock seconds.
- The mechanism by which the RTC 1 Hz interrupt drives boundary events is unchanged.
- ISR discipline is unchanged: `rtc_second_cb()` still only performs the GET_TIME ioctl and
  `app_event_publish_isr()`.
- The business record layout of `health_pedo_record_t` is unchanged; but
  `FDB_USING_TIMESTAMP_64BIT` must be enabled, so the FlashDB TSDB sector header and log index layout
  change from 32-bit to 64-bit timestamps. Upgrading requires rebuilding the pedo TSDB; the old index cannot be read
  directly under the new layout.

## 6. Phone changes (HoneyBox)

### 6.1 `lib/services/watch_time_protocol.dart`

`buildSetTime(DateTime localTime)` changes to send 4 bytes of big-endian seconds:

```dart
final seconds = DateTime.utc(
  localTime.year, localTime.month, localTime.day,
  localTime.hour, localTime.minute, localTime.second,
).millisecondsSinceEpoch ~/ 1000;
```

**Key point: `DateTime.utc(...)` must be used rather than `DateTime(...)`.**
The latter applies the device timezone offset a second time, producing the wrong seconds.
What is taken here are the **calendar fields** of `localTime`, converted by UTC rules,
which is exactly the anchor generation defined in §3.1.

Delete the packed bit operations. The `minimumYear`/`maximumYear` checks may stay as
sanity checks, but are no longer a format constraint.

The call site `lib/providers/watch_bind_provider.dart:141`
(`WatchTimeProtocol.buildSetTime(_clock())`, where `_clock` defaults to
`DateTime.now`) needs no change.

### 6.2 `lib/services/watch_health_protocol.dart`

The current `_readTimestamp()` (lines 301-307):

```dart
return DateTime.fromMillisecondsSinceEpoch(seconds * 1000, isUtc: true)
    .toLocal();
```

**Drop `.toLocal()`** and keep `isUtc: true`. The seconds already are the instant to display,
and applying `toLocal()` again would apply the timezone offset a second time. Also update the comment on line 300,
"Records are UTC on the wire".

### 6.3 `lib/pages/watch/health/watch_health_data.dart` —— must be reviewed as well

After dropping `.toLocal()`, `record.timestamp` becomes a UTC-flagged
`DateTime`. Dart's `.hour`/`.year` and similar accessors return UTC fields for UTC-flagged
objects and local fields for local objects —— so the record side is automatically correct,
but **places that compare against a local `DateTime` will be wrong**.

Places to review and fix:

- line 103 `_today`: constructed from `syncedAt`, and `syncedAt` comes from
  `watch_health_repository.dart:142`'s `_clock()` (a local `DateTime.now`).
  `_today` must be made UTC-flagged to compare correctly against records.
- lines 106 and 235 `_sameDate(record.timestamp, ...)`
- lines 209-210, the period filtering on `record.timestamp.hour`
- line 230 `_today.subtract(...)`
- line 258, the `_sameDate()` implementation itself

Fix principle: make both sides of every comparison UTC-flagged, i.e. construct the `syncedAt`
side with wall clock semantics as well. Check each place individually when implementing; do not do a bulk replace.

### 6.4 Display paths

`watch_health_page.dart:569` `_recordTime()` and lines 78/124
`_date()`/`_time()` format the `DateTime` fields directly,
which on a UTC-flagged object outputs the correct wall time and needs no change —— but confirm by testing
that it does not go through `toLocal()`.

## 7. Trade-offs and accepted costs

**Discontinuity when travelling across timezones.** When the phone sends wall clock seconds for a new timezone the seconds jump,
historical records overlap or leave gaps at the jump, and records from different timezone segments cannot be compared on the same
absolute timeline.

Why this cost is accepted: every record always equals "the time the watch face showed at that moment" and is therefore always explainable;
whereas the alternative (storing true UTC), given a protocol that carries no timezone, would have to infer UTC back from a timezone
hard-coded into the firmware, and a wrong guess bakes the error permanently into Flash with no way to
recover afterwards. This scheme never needs to know the timezone, so it can never guess wrong.

**Breaking change.** Mixing old and new firmware/phone silently shifts times by one timezone, with no error.
This is confirmed as accepted: the project is in development, devices can be reflashed at any time, and both sides are released together.
The incompatibility is recorded in the document. The protocol introduces no version gate (the SPORT command already has the
version=1 precedent, but it is not used here).

**"Everything uses seconds" with zero exceptions.** All time fields in the protocol are unified as 1970-epoch wall clock seconds:
the alarm definitions are deleted (§4.3) and 0x21 key test is switched to the 1970 epoch (§4.4).
The document needs to keep no "but there is an exception somewhere" note.

## 8. Verification

### 8.1 Round-trip consistency tests (one per side)

Core invariant: **the seconds computed by the phone, expanded by the firmware's `gmtime_r`,
must reproduce the phone's original local calendar fields.**

- Phone side: extend `test/services/watch_time_protocol_test.dart`
  and assert that the 4 bytes of seconds generated from a given local time match the expected constant values.
- Firmware side: assert that those seconds go back through `gmtime_r` to the same year, month, day, hour, minute and second.
- Phone side: extend `test/services/watch_health_protocol_test.dart`
  and assert that the `DateTime` fields returned by `_readTimestamp()` for given seconds
  match the expected wall time, and do not vary with the timezone of the runtime environment.

The last one matters especially: the tests must pass under any `TZ`,
which is the executable definition of "the timezone does not enter the data flow".

### 8.2 Defect regression confirmation

- **Day rollover instant**: verify that `EVT_TIME_DAY_CHANGED` fires at wall clock 00:00,
  not at 16:00 (defect 1 in §1.1).
- **Sync log**: verify that the SPORT sync log prints the same time as the watch face,
  no longer shifted by 8 hours (defect 2 in §1.1) —— this is the cheapest end-to-end verification point.
- **Quarter-hour boundary**: confirm that `EVT_TIME_TICK_15MIN` behaviour has not regressed.

### 8.3 Compatibility with existing data

`ts_utc` → `ts` is itself a pure rename; but once the sport record Timestamp changes from a closing boundary to a logical bucket
start, old and new records cannot be interpreted together. In addition, the default `fdb_time_t` is signed
32-bit, so values after 2038 become negative. The firmware must
enable `FDB_USING_TIMESTAMP_64BIT`, which changes the FlashDB sector header and log index
layout, so an old TSDB partition cannot be made compatible in place.

The current development stage adopts a one-shot rebuild strategy: the KVDB stores the pedo layout version; when the 64-bit timestamp
layout or the sport bucket Timestamp semantics change, the version is bumped, and the first boot of the new firmware merely erases
`fdb_tsdb1`, zeroes the `health.synced` watermark, and finally
writes the new layout version. Other KVDB configuration and BF data are unaffected. The marker is written last, so a
power loss during migration retries safely on the next boot. If history must be preserved after mass production, an offline
export/import should be implemented separately; it must not rely on FlashDB automatically recognising the old layout.
