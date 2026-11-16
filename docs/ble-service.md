# BLE service and navigation projection

The wire protocol itself is specified in the published **HoneyProtocol** document
(<https://realmcu.github.io/HoneyProtocol/>, last updated 2026-11-09). That document is the
authority for the L0/L1/L2 framing and for every L2 command layout. This guide covers what
*this firmware* exposes on top of it: the advertising identity, the HMIS GATT service, and
the navigation projection session, including the places where the implementation deviates
from the specification.

Source of truth for this guide:

| Area | File |
| --- | --- |
| Advertising / scan response | `src/hmi_rtk_bt/hmi_ble/hmi_ble_gap_init.c` |
| HMIS service, characteristics | `src/hmi_rtk_bt/hmi_ble/private_service/hmi_ctrl_service.c` |
| L0 carrier binding (`0xFFC2` notify) | `src/hmi_rtk_bt/hmi_app/hmi_ble_ctrl.c` |
| L1 framing and CRC | `src/protocol/hmi_proto.c` |
| L2 routing | `src/protocol/hmi_l2.c` |
| Projection session, JPEG validation, UI | `src/hmi_rtk_bt/hmi_ble/private_service/navi_projection_service.c` |
| Generic L2 command IDs | `src/protocol/hmi_l2.h` |

## What the device exposes

| Service | UUID | Direction | Purpose |
| --- | --- | --- | --- |
| HMIS private service | `484D4953-0000-1000-8000-00805F9B34FB` | bidirectional | Control channel and the projection carrier |
| OTA service | vendor OTA UUIDs from the SDK (`ota_add_service`) | bidirectional | Firmware upgrade over BLE |
| Nordic UART (NUS) | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | bidirectional | Registered for compatibility; no module in this project sends on it |
| SPP (BR/EDR) | — | bidirectional | Upgrade transport used by `src/ota/` |

Only the HMIS service is described here. BLE and BR/EDR are both enabled; BR/EDR carries
A2DP (sink and source), AVRCP, HFP, SPP and PAN under `src/hmi_rtk_bt/hmi_app/`.

## Advertising and scan response

`ADV_IND` is 31 bytes and completely full, so the local name is only in the scan response.
Advertising interval is fixed at `320 × 0.625 ms = 200 ms` (min = max).

| Packet | Field | Bytes |
| --- | --- | --- |
| ADV_IND | Flags: LE general discoverable, BR/EDR not supported | 3 |
| ADV_IND | Complete list of 128-bit service UUIDs: HMIS | 18 |
| ADV_IND | Manufacturer specific data (company ID `0x005D` + 6-byte payload) | 10 |
| SCAN_RSP | Complete local name `HMI-Dashboard` | 14 |

Manufacturer payload layout:

| Offset | Field | Notes |
| --- | --- | --- |
| 0 | Payload version | `0x01` |
| 1 | Model | `0x01` RTL8773G, `0x02` RTL8773E, `0x03` RTL8782 |
| 2 | Capabilities | bit0 projection, bit1 bonded, bit2 charging |
| 3 | Battery | percent, updated at run time |
| 4..5 | Short device ID | low 16 bits of the BD address, LSB first |

Pairing uses LE bonding with `NoInputNoOutput` I/O capability. The device requests an MTU
exchange as soon as a link is up (`GAP_PARAM_SLAVE_INIT_GATT_MTU_REQ`).

## HMIS private service

This matches §2.2.1 of the specification.

| Characteristic | UUID | Property | Contents |
| --- | --- | --- | --- |
| Command | `0xFFC1` | Write / Write No Rsp | Phone → device, up to 512 bytes |
| Event | `0xFFC2` | Notify (+ CCCD) | Device → phone, one L1 frame per notification |
| Status | `0xFFC3` | Read | Device status, up to 32 bytes, user description `HMIS Read` |

Notifications only flow after the phone has written the CCCD on `0xFFC2`. Write and notify
are symmetric data channels at the protocol level: both carry the same L2 frames, and the
read characteristic never carries a data stream.

## HoneyProtocol layering

All multi-byte fields are **big-endian**.

- **L0** — the GATT transport. On this device it is the HMIS characteristics above: phone
  writes `0xFFC1`, device notifies `0xFFC2` (`hmi_l1_carrier_tx()` in `hmi_ble_ctrl.c`).
- **L1** — 8-byte header + payload, implemented once in `src/protocol/hmi_proto.c`:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Magic `0xAB` |
| 1 | 1 | bit7 reserved, bit6 ACK request, bit5 error, bit4 ACK, bits3..0 version (`0`) |
| 2..3 | 2 | Payload length |
| 4..5 | 2 | CRC-16/ARC (init `0x0000`, reflected `0xA001`), **payload only**; check value `0xBB3D` for `"123456789"` |
| 6..7 | 2 | Sequence number |
| 8..N | N | Payload |

- **L2** — 5-byte header + value, carried inside one L1 frame:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Command |
| 1 | 1 | Version (high nibble) / reserved (low nibble) |
| 2 | 1 | Key |
| 3..4 | 2 | Value length |
| 5..N | N | Value |

**Implementation limits versus the specification.** The specification lets L1 reassemble up
to 65535 bytes and leaves segmentation on L0 open. This firmware does neither: one L1 frame
must fit in a single write/notification, and the projection caps the L1 payload at 248 bytes
(250 with the header). The phone must therefore keep every write inside the negotiated MTU.

## L2 commands used by this firmware

`src/protocol/hmi_l2.c` is the router: it parses the 5-byte L2 header, answers `0x05` itself,
offers application-owned frames to the projection handler, and otherwise logs the command as
unimplemented. Two command IDs are in use: `0x05` (device information) and `0x10` (frame
transfer). `0x11` is not used — it belongs to the specification's point test.

### `0x05` — device information (the projection handshake)

Header reads `05 10` (L2 version 1). The phone asks once per connection and gets every limit
it needs to choose a resolution, an encoder rate and a chunk size.

| Key | Name | Direction | Value |
| --- | --- | --- | --- |
| `0x0B` | device info request | phone → device | empty |
| `0x2B` | device info answer | device → phone | TLV list |

Each TLV is `id (1) + length (2, big-endian) + value`; unknown ids are ignored, so fields can
be appended later without a version bump.

| id | Field | Length | Value on this build |
| --- | --- | --- | --- |
| `0x01` | Projection area | 4 | `u16 width`, `u16 height` — **410 × 370**, the geometry of the Designer UI's `gui_stream` widget |
| `0x02` | JPEG limit | 4 | `u32` = **40960** — the largest *compressed* JPEG that fits one transport buffer (the pool is 5 × 40 KB); 131072 is only the compile-time ceiling |
| `0x03` | JPEG quality | 1 | **60** — the quality the phone should encode with so a 410 × 370 frame stays inside the 40 KB buffer |
| `0x04` | Features | 4 | bit0 projection, bit1 QR pairing, bit2 device validates CRC32 + JPEG |
| `0x05` | Chunk limit | 2 | 241 bytes |
| `0x06` | Requested fps | 1 | **3** — the sampling rate the device asks the phone to use |

The answer is 34 bytes of value (47 bytes on the air), so it always fits one L1 frame. Example (L2 part):

```
request : 05 10 0B 00 00
answer  : 05 10 2B 00 22
          01 00 04 01 9A 01 72     area 410 x 370
          02 00 04 00 00 A0 00     jpeg max 40960
          03 00 01 3C              recommended quality 60
          04 00 04 00 00 00 07     features: projection + QR + device check
          05 00 02 00 F1           chunk max 241
          06 00 01 03              requested 3 fps
```

`0x02` is the encoded file size, not the decoded picture: the decoder allocates the component
planes plus an RGB888 frame and checks that against the 2.5 MB GUI heap (410 × 370 peaks at
roughly 0.75 MB), so the 40 KB transfer cap never constrains the decode.

The phone adopts `0x06` as its sampling rate and `0x03` as the encoder quality; both must be
set before the first frame because the capture surface and the JPEG encoder are fixed when the
virtual display starts. The phone
should re-query after a reconnect. A device that does not answer within ~1 s is
running older firmware: fall back to the local defaults and keep streaming, `0x10` is
unchanged.

### `0x10` — file transfer (one JPEG frame per session)

This is §3.12 of the specification, used here as the projection data path: every map frame
is sent as a complete JPEG file, and the next frame starts a new session.

| Key | Name | Direction | Value |
| --- | --- | --- | --- |
| `0x01` | BEGIN request | phone → device | `file_type` (1), `total_size` (4), `chunk_size` (2), `name_len` (1), `name` (0…n) |
| `0x02` | BEGIN response | device → phone | `status` (1), `chunk_size` (2) |
| `0x03` | DATA | phone → device | `seq` (2, from 0), up to `chunk_size` bytes |
| `0x05` | END request | phone → device | CRC32 of the whole file (4) |
| `0x06` | END response | device → phone | `status` (1), `error_code` (1) |
| `0x07` | ABORT | both | `reason` (1): `0x00` user, `0x01` error, `0x02` timeout |

Rules the device enforces:

- `file_type` must be `0x01`…`0x03` (image / video / raw); the projection expects `0x01`.
- `total_size` must be non-zero and fit the stream transport buffer (≤128 KB).
- `chunk_size` is a request in 1…2048. The device answers with what the current MTU can
  carry — `MTU − 3 − 8 (L1) − 5 (L2) − 2 (seq)` — and the phone must use the value from the
  BEGIN response, not its own request. With MTU 247 that is 229 bytes; the compile-time
  ceiling is 241.
- One session at a time; a second BEGIN gets `status = 0x01` (busy).
- DATA sequence numbers must increase by exactly one; anything else raises
  `ABORT (reason = 0x01)`.
- END checks total length and the CRC32 (poly `0xEDB88320`, big-endian, whole file), then
  validates the payload as a baseline JPEG: SOF dimensions must equal the first committed
  frame's, `width × height` ≤ 192 000, and the decode must fit the GUI heap.
- Ten seconds without data abort the session with `reason = 0x02`.

Status codes: BEGIN `0x00` accepted, `0x01` busy, `0x02` no space, `0x03` bad type.
END `0x00` ok, `0x01` CRC failure, `0x02` incomplete; `error_code` `0x00` none, `0x01`
write failed, `0x02` decode failed.

## Session sequence

```
phone                                   device
  |-- 0x05 GET_INFO (0x0B) ------------->|
  |<------------ 0x05 INFO (0x2B, TLV) --|   area, jpeg max, fps/quality, chunk
  |-- 0x10 BEGIN (type,size,chunk,name)->|
  |<------------ 0x10 BEGIN_RSP(chunk) --|
  |-- 0x10 DATA (seq 0 ... ) ----------->|   (repeat until total_size)
  |-- 0x10 END (crc32) ----------------->|
  |<------------ 0x10 END_RSP(status) ---|   frame committed to the GUI
  |        ... next frame = new BEGIN/DATA/END cycle ...
```

## QR pairing page

Until the first frame is committed, the Designer UI keeps a QR page on screen. The URL is
built from the finalized public address (`navi_projection_build_qr_url()`):

```
https://github.com/realmcu/HoneyBox/releases/latest/download/HoneyBox.apk
```

The page carries model `RTL8773E` and serial `0529`; the phone app takes the device address
from the QR content. The page is hidden only after a frame is committed
(`navi_gui_stream_active_cb`), which is why a rejected frame leaves the QR visible instead of
showing a black map.

## Build configuration

The projection is compiled in when the Designer UI is used:

```ini
CONFIG_REALTEK_BUILD_DASHBOARD_DESIGNER=y   # selects the projection automatically
CONFIG_REALTEK_APP_NAVI_PROJECTION=y        # selects the stream and qbcode widgets
```

`CONFIG_REALTEK_APP_NAVI_PROJECTION` is not user-selectable: `Kconfig` enables it from
`REALTEK_BUILD_DASHBOARD_DESIGNER`. Turning the Designer UI off also removes the projection
sources from the build (`src/hmi_rtk_bt/hmi_ble/private_service/CMakeLists.txt`).

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Service visible, no notifications | CCCD on `0xFFC2` not enabled |
| No answer to `0x05 / 0x0B` | Device runs firmware older than this handshake — keep the local defaults, `0x10` still works |
| `0x2B` malformed or empty area | Device reported 0 × 0; keep the local defaults rather than encoding for nothing |
| BEGIN answered with `0x01` (busy) | Previous file session not closed; wait for END_RSP or send ABORT |
| BEGIN answered with `0x02` (no space) | `total_size` exceeds the stream transport buffer (128 KB) |
| Device aborts with `reason = 0x01` | DATA `seq` gap, or a write past `total_size` |
| END answered with `0x02` (incomplete) | Fewer bytes arrived than `total_size` |
| END answered with `0x01` (CRC) | CRC32 mismatch — check the polynomial (`0xEDB88320`) and that it covers the whole file |
| END ok but screen stays on the QR page | Decode failed (geometry or heap) — look for `navi_file: END rejected geometry` |
| Frames dropped right after connecting | `chunk_size` larger than the MTU allows; use the value from BEGIN_RSP |

Log lines are prefixed `Navi` / `navi_file` (`APP_PRINT_*` in
`navi_projection_service.c`): `Navi OPEN rejected`, `navi_file: BEGIN rejected`,
`navi_file: DATA out of order`, `navi_file: END rejected geometry`.

## Notes for maintainers

- The earlier frame-slot transport (`navi_handle_frame_packet`, `NAVI_FRAME_SLOTS`, credits,
  `REPORT`) is still in `navi_projection_service.c` but unreachable: every `0x10` frame is
  routed to the file-transfer path. Do not document or rely on it.
- `0x11` was removed: the projection session no longer needs OPEN/CLOSE because `0x05/0x0B`
  reports the geometry and every frame carries its own JPEG header. The old `navi_open_params_t`
  and its frame-slot helpers are still in the file but unused.
- L1 used to be implemented twice (here and in `src/protocol`). It is now a single
  implementation in `hmi_proto.c` with CRC-16/ARC over the payload; the projection only
  encodes and decodes through `proto_l2_build()` / the registered sink. There is no task,
  queue or heap allocation on the frame path, so streaming costs the same as before.
- The generic command table in `hmi_l2.c` (OTA, settings, bind, notify, sport, factory,
  control, log) still has no business logic: it logs and answers nothing, so a phone request
  times out instead of getting a fake success. Implementing a command means adding its
  handler there, not in the projection engine.
