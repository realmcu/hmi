# Smart Bracelet BLE Private Communication Protocol

> Original document: Baidu Smart Bracelet Bluetooth Private Communication Protocol (August 2014)  
> Module owner: Chen Xixiong · Project owner: Sun Hefei  
> Copyright: Baidu Online Network Technology (Beijing) Co., Ltd.

---

## Table of Contents

1. [Terminology and Conventions](#1-terminology-and-conventions)
2. [Protocol Structure Overview](#2-protocol-structure-overview)
3. [L2 Command Details](#3-l2-command-details)
4. [Appendix: Key Data Structure Quick Reference](#appendix-key-data-structure-quick-reference)

---

## 1 Terminology and Conventions

### 1.1 Terminology

| Term | Description |
|------|-------------|
| **Device** | Bracelet, watch, smart scale, etc.; usually the Bluetooth Master side |
| **Phone** | A BLE-capable smartphone, or possibly a Bluetooth USB Dongle |

### 1.2 Conventions

#### 1.2.1 Byte Order

**All multi-byte fields in this protocol use Big-Endian.**

Example: `uint16_t a = 0xABCD`; the transmitted byte stream from low to high is: `A, B, C, D`.

If one byte contains two values (a=0xa occupies the low 4 bits, b=0xb occupies the high 4 bits), the byte stream content (binary) is: `1011 1010`.

#### 1.2.2 L2 V-length Note

When the value of a key under a command is empty, v-length = 0.  
The Key Header always occupies **2 bytes**; when v-length = 0 these 2 bytes must still be sent and cannot be omitted.

---

## 2 Protocol Structure Overview

### 2.1 Protocol Stack Structure

```
┌─────────────────────────────┐
│   Application layer (L2)    │
├─────────────────────────────┤
│   Transport Layer (L1)      │
├─────────────────────────────┤
│   UART Profile (L0)         │
├─────────────────────────────┤
│   BLE Stack                 │
└─────────────────────────────┘
```

---

### 2.2 L0 — UART Profile (BLE)

#### GATT Service UUID

| Role | UUID |
|------|------|
| UART Profile Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| Write Characteristic | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| Read Characteristic  | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` (Notification mode) |

#### Description

- **Device side (Master)**: implements the UART Profile and provides two characteristics, write and read
- **Phone side (Slave)**: implements the write interface and receive interface based on character operations
- Phone-side receive uses **notify** mode; Device-side receive uses **write character** (push mode, not pull)
- MTU = **20 bytes**

---

### 2.3 L1 — Transport Layer

#### Functions

- Provides reliable transport (send + receive) on top of L0
- L0 MTU = 20 bytes; L1 supports a maximum payload of **65535 bytes** via packet fragmentation/reassembly (16-bit length field)
- Implements an **ACK mechanism**: the receiver sends a success/error ACK after validation
- Retransmission: resend after receiving an error ACK or timing out; report failures to L2

#### L1 Packet Structure

```
┌───────────────────────────────────────────────┐
│         L1 Packet (8 ~ 65543 bytes)           │
├──────────────────────────┬────────────────────┤
│   L1 Header (8 bytes)    │  L1 Payload        │
│                          │  (0 ~ 65535 bytes) │
└──────────────────────────┴────────────────────┘
```

#### L1 Header (8 bytes, Big-Endian)

| Byte | Bit field | Width | Description |
|------|-----------|-------|-------------|
| 0 | Magic | 8 bits | Fixed value `0xAB` |
| 1 [7:6] | Reserve | 2 bits | Reserved |
| 1 [5] | ERR flag | 1 bit | 1 = transmission error |
| 1 [4] | ACK flag | 1 bit | 1 = this is an ACK packet |
| 1 [3:0] | Version | 4 bits | Current version = 0 |
| 2~3 | Payload length | 16 bits | Big-Endian, high byte first |
| 4~5 | CRC16 | 16 bits | Covers Header[0..3] + Payload, Big-Endian |
| 6~7 | Sequence ID | 16 bits | Packet sequence number, Big-Endian |

#### ACK Packet Format

| Packet type | ACK flag (bit4) | ERR flag (bit5) | Payload length |
|-------------|-----------------|-----------------|----------------|
| Data packet | 0 | 0 | Actual payload length |
| Success ACK | 1 | 0 | 0 |
| Error ACK | 1 | 1 | 0 |

> The Sequence ID of an ACK packet = the Sequence ID of the data packet being ACKed.

#### L1 Version: **0**

---

### 2.4 L2 — Application Layer

#### L2 Packet Structure

```
┌───────────────────┬───────────────────────────┐
│  L2 Header (2B)   │  L2 Payload (0~65533 B)   │
└───────────────────┴───────────────────────────┘
```

> The L2 Payload upper bound is constrained by the L1 payload capacity (max 65535 B); the actual usable size depends on the device firmware buffer.

#### L2 Header (16 bits, Big-Endian)

| Bit field | Width | Description |
|-----------|-------|-------------|
| Command ID | 8 bits | Command type, see the Command list |
| Version | 4 bits | L2 version number |
| Reserve | 4 bits | Reserved |

#### L2 Payload Structure

```
┌─────┬────────────┬───────────┬─────┬────────────┬───────────┬─────┐
│ Key │ Key Header │ Key Value │ Key │ Key Header │ Key Value │ ... │
│ 1B  │   2 bytes  │  N bytes  │ 1B  │   2 bytes  │  N bytes  │     │
└─────┴────────────┴───────────┴─────┴────────────┴───────────┴─────┘
```

#### Key Header (16 bits)

| Bit field | Width | Description |
|-----------|-------|-------------|
| Key Value Length (v-length) | 16 bits | Number of bytes in this key's value, max 65535 |

> The original design used [15:9] as Reserve and [8:0] as v-length (9 bits, max 511 bytes).  
> It has been extended to a full 16-bit v-length to support large transfers (≥512 bytes). Both ends must be updated in sync.

---

## 3 L2 Command Details

### 3.1 Command List

| Command ID | Command |
|-----------|------|
| `0x01` | Firmware upgrade command |
| `0x02` | Settings command |
| `0x03` | Binding command |
| `0x04` | Alert command |
| `0x05` | Sport data command |
| `0x06` | Factory test command |
| `0x07` | Control command |
| `0x08` | Dump Stack command |
| `0x09` | Test Flash read command |
| `0x0a` | Log command |
| `0x0b` | File transfer command |
| `0x0c` | BLE connection parameter command |
| `0x0d` | WiFi provisioning command |

---

### 3.2 Firmware Upgrade Command (command id 0x01)

L2 version: **0**

| Key | Definition |
|-----|------|
| `0x01` | Enter firmware upgrade mode request |
| `0x02` | Enter firmware upgrade mode response |

#### 0x01 — Enter Firmware Upgrade Mode Request

**Direction**: Phone → Device | **Value**: empty (v-length = 0)

#### 0x02 — Enter Firmware Upgrade Mode Response

**Direction**: Device → Phone | **Value (2 bytes)**:

| Field | Size | Description |
|------|------|------|
| Status code | 1 byte | `0x00`=OTA success; `0x01`=OTA failure |
| Error code | 1 byte | `0x01`=battery too low (valid only when status=0x01) |

---

### 3.3 Settings Command (command id 0x02)

L2 version: **0**

| Key | Definition |
|-----|------|
| `0x01` | Time setting |
| `0x02` | Alarm setting |
| `0x03` | Get device alarm list request |
| `0x04` | Get device alarm list response |
| `0x05` | Step goal setting |
| `0x10` | User profile setting |
| `0x20` | Anti-loss setting |
| `0x21` | Sedentary reminder setting |
| `0x22` | Left/right hand wearing setting |
| `0x23` | Phone OS setting |
| `0x24` | Incoming call notification phone list setting |
| `0x25` | Incoming call notification switch setting |

#### 0x01 — Time Setting

**Direction**: Phone → Device (must sync after each successful binding) | **Value (32 bits)**:

| Field | Width | Valid values |
|------|------|--------|
| Year | 6 bits | 0~63 (from year 2000; 13=2013) |
| Month | 4 bits | 1~12 |
| Day | 5 bits | 1~31 |
| Hour | 5 bits | 0~23 |
| Minute | 6 bits | 0~59 |
| Second | 6 bits | 0~59 |

#### 0x02 / 0x04 — Alarm Setting / Get Alarm List Response

Supports up to **8 alarms**. **Value (5×N bytes)**, each Alarm item (40 bits):

| Field | Width | Description |
|------|------|------|
| Year | 6 bits | From year 2000 |
| Month | 4 bits | 1~12 |
| Day | 5 bits | 1~31 |
| Hour | 5 bits | 0~23 |
| Minute | 6 bits | 0~59 |
| Id | 3 bits | Alarm index (0~7) |
| Reserve | 4 bits | — |
| Day flags | 7 bits | Low→high bits correspond to Monday→Sunday; 1=repeat, 0=do not repeat; all 0=valid only for the current day |

#### 0x03 — Get Device Alarm List Request

**Value**: empty

#### 0x05 — Step Goal Setting

**Value (4 bytes, Big-Endian)**:  
`Target = Byte1<<24 | Byte2<<16 | Byte3<<8 | Byte4`

#### 0x10 — User Profile Setting

**Value (32 bits)**:

| Field | Width | Description |
|------|------|------|
| Gender | 1 bit | 0=female; 1=male |
| Age | 7 bits | 0~127 |
| Height | 9 bits | 0.0~256.0 cm (resolution 0.5 cm) |
| Weight | 10 bits | 0.0~512.0 kg (resolution 0.5 kg) |
| Reserved | 5 bits | — |

#### 0x20 — Anti-loss Setting

**Value (8 bits)**: low 4 bits = Mode: `0x0`=no alert; `0x1`=middle alert; `0x2`=high alert

#### 0x21 — Sedentary Reminder Setting

**Value (8 bytes)**:

| Field | Size | Description |
|------|------|------|
| Reserved | 1 byte | — |
| Enable switch | 1 byte | `0x00`=off; `0x01`=on |
| Threshold | 2 bytes | Step count threshold (0~65535) |
| Sedentary time | 1 byte | Minutes; alert only after exceeding |
| Reminder start time | 1 byte | Hour (0~23) |
| Reminder end time | 1 byte | Hour (0~23) |
| Day flags | 1 byte | Low→high bits correspond to Monday→Sunday; all 0=current day only |

#### 0x22 — Left/Right Hand Wearing Setting

**Value (1 byte)**: `0x01`=left hand; `0x02`=right hand

#### 0x23 — Phone OS Setting

**Value (2 bytes)**: Byte0: `0x01`=iOS; `0x02`=Android; Byte1: Reserved

#### 0x24 — Incoming Call Notification Phone List Setting

**Value (1+20×N bytes, N≤3)**: operation type (1 byte) + name or phone number (20 bytes×N)  
Operation type: `0x01`=add; `0x02`=replace

#### 0x25 — Incoming Call Notification Switch

**Value (1 byte)**: `0x01`=enable; `0x02`=disable

---

### 3.4 Binding Command (command id 0x03)

L2 version: **0**

| Key | Definition |
|-----|------|
| `0x01` | Bind user request |
| `0x02` | Bind user response |
| `0x03` | User login request |
| `0x04` | User login response |
| `0x05` | User unbind |
| `0x06` | Super binding |
| `0x07` | Super binding response |

#### 0x01 — Bind User Request

**Value**: 32-byte user ID (aligned to the low bytes, padded with `0x00` if shorter)

#### 0x02 — Bind User Response

**Value (1 byte)**: `0x00`=success; `0x01`=timeout failure

#### 0x03 — User Login Request

**Value**: 32-byte user ID (same as above)

#### 0x04 — User Login Response

**Value (1 byte)**: `0x00`=success; `0x01`=ID mismatch failure

#### 0x05 — User Unbind

**Value**: 1 byte reserved

#### 0x06 — Super Binding

**Value (64 bytes)**: User ID (32 bytes) + Super key (32 bytes)

#### 0x07 — Super Binding Response

**Value (1 byte)**: `0x00`=success; `0x01`=timeout; `0x02`=Super key validation failed; `0x03`=low battery

---

### 3.5 Alert Command (command id 0x04)

L2 version: **0**

| Key | Definition | Direction | Value |
|-----|------|------|-------|
| `0x01` | Incoming call alert | Phone→Device | empty |
| `0x02` | Incoming call answered | Phone→Device | empty |
| `0x03` | Incoming call rejected | Phone→Device | empty |

---

### 3.6 Sport Data Command (command id 0x05)

L2 version: **0**

| Key | Definition |
|-----|------|
| `0x01` | Request data (Phone→Device) |
| `0x02` | Sport data response (Device→Phone) |
| `0x03` | Sleep data response (Device→Phone) |
| `0x04` | More flag (Device→Phone, more data available) |
| `0x05` | Sleep setting data response (Device→Phone) |
| `0x06` | Real-time data sync setting (Phone→Device) |
| `0x07` | History data sync start (Device→Phone) |
| `0x08` | History data sync end (Device→Phone) |
| `0x09` | Current day sport status sync (Phone→Device) |
| `0x0a` | Most recent sport status sync (Phone→Device) |
| `0x0b` | Current day total sport data calibration (Device→Phone) |
| `0x0c` | Current day total sport data calibration response (Phone→Device) |

#### 0x01 — Request Data

**Value**: empty

#### 0x02 — Sport Data Response

**Value**: Sport data header (4 bytes) + N × Sport item (8 bytes)

**Sport data header (32 bits)**:

| Field | Width | Description |
|------|------|------|
| Date | 16 bits | See the Date definition |
| Reserve | 8 bits | — |
| Sport Item count | 8 bits | Number of following sport items |

**Date (16 bits)**:

| Field | Width | Description |
|------|------|------|
| Reserve | 1 bit | — |
| Year | 6 bits | 0~63, from year 2000 |
| Month | 4 bits | 1~12 |
| Day | 5 bits | 1~31 |

**Sport item (8 bytes = 64 bits)**:

| Field | Width | Description |
|------|------|------|
| Offset | 11 bits | From 00:00 each day, +1 every 15 minutes |
| Mode | 2 bits | Sport mode |
| Step count | 12 bits | Step count |
| Active time | 4 bits | Active time |
| Calory | 19 bits | Calories |
| Distance | 16 bits | Distance |

#### 0x03 — Sleep Data Response

**Value**: Sleep data header (4 bytes) + N × Sleep item (4 bytes)

**Sleep data header (32 bits)**: Date (16 bits) + Sleep Item count (16 bits)

**Sleep item (32 bits)**:

| Field | Width | Description |
|------|------|------|
| Minutes | 16 bits | Minutes since 00:00 of Date |
| Reserve | 12 bits | — |
| Mode | 4 bits | `0x01`=deep sleep; `0x02`=light sleep; `0x03`=not asleep |

#### 0x04 — More flag

**Value**: empty (more data available; the phone should keep requesting)

#### 0x05 — Sleep Setting Data Response

**Value**: Sleep setting data header (4 bytes) + N × Sleep setting item (4 bytes)

**Sleep setting item (32 bits)**:

| Field | Width | Description |
|------|------|------|
| Minutes | 16 bits | Minutes since 00:00 of Date |
| Reserve | 12 bits | — |
| Mode | 4 bits | `0x00`=enter sleep; `0x01`=exit sleep |

#### 0x06 — Real-time Data Sync Setting

**Value (1 byte)**: `0x00`=off; `0x01`=on

#### 0x07 / 0x08 — History Data Sync Start / End

**Value**: empty

#### 0x09 — Current Day Sport Status Sync

**Value (12 bytes)**: total steps (4B) + total distance in m (4B) + total calory in 1/1000 KA (4B)

#### 0x0a — Most Recent Sport Status Sync

**Value (Sport item, 10 bytes)**: Mode (1B) + Active time (1B) + Calory (4B) + Step count (2B) + Distance (2B)

#### 0x0b — Current Day Total Sport Data Calibration

**Value (Calibrate Record, 16 bytes)**:

| Field | Width | Description |
|------|------|------|
| offset | 8 bits | Current offset (0~96) |
| Total steps today | 24 bits | steps |
| Total calories today | 24 bits | 0.001 cal |
| Total distance today | 24 bits | m |
| Current 15-minute steps | 16 bits | steps |
| Current 15-minute calories | 16 bits | 0.001 cal |
| Current 15-minute distance | 16 bits | m |

#### 0x0c — Current Day Total Sport Data Calibration Response

**Value (Calibrate delta value, 7 bytes)**:

| Field | Width | Description |
|------|------|------|
| offset | 8 bits | Current offset (0~96) |
| delta_steps | 16 bits | Step difference from the previous offset |
| delta_calories | 16 bits | Difference (0.001 cal) |
| delta_distance | 16 bits | Difference (m) |

---

### 3.7 Factory Test Command (command id 0x06)

L2 version: **0**

| Key | Definition |
|-----|------|
| `0x01` | Request echo service |
| `0x02` | Echo service response |
| `0x03` | Request charge info |
| `0x04` | Return charge info |
| `0x05` | Turn on LED |
| `0x06` | Vibrate motor |
| `0x07` | Write SN |
| `0x08` | Read SN |
| `0x09` | SN response |
| `0x0a` | Write test flag |
| `0x0b` | Read test flag |
| `0x0c` | test flag response |
| `0x0d` | Request sensor data |
| `0x0e` | Return sensor data |
| `0x10` | Enter test mode (super command) |
| `0x11` | Exit test mode (super command) |
| `0x21` | Button test |
| `0x31` | Motor aging test |
| `0x32` | LED aging test |

#### 0x01/0x02 — Echo

Request Value: N-byte string; response Value: returned unchanged

#### 0x03/0x04 — Charge Info

Request Value: empty; response **Value (2 bytes)**: Voltage (16 bits; a negative value means not detected)

#### 0x05 — Turn On LED

**Value**: empty=turn on all LEDs; `0x00`=first row; `0x01`=second row; ...

#### 0x06 — Vibrate Motor

**Value**: empty

#### 0x07/0x08/0x09 — SN Write/Read/Response

Write request/response Value: 32-byte SN; read request Value: empty

#### 0x0a/0x0b/0x0c — test flag Write/Read/Response

Write request/response Value: 1-byte flag; read request Value: empty

#### 0x0d/0x0e — sensor Data Request/Response

Request Value: empty; response **Value (48 bits)**: X axis (16b) + Y axis (16b) + Z axis (16b)

#### 0x10/0x11 — Enter/Exit Test Mode

**Value**: 32-byte token (executed only after the device validates it)

#### 0x21 — Button Test

**Direction**: Device → Phone | **Value (8 bytes)**:

| Field | Size | Description |
|------|------|------|
| Code | 1 byte | `0x00`=DOWN; `0x01`=UP; `0x02`=SHORT_CLICK; `0x03`=LONG_CLICK |
| Id | 1 byte | Button ID |
| Reserved | 2 bytes | — |
| Timestamp | 4 bytes | Seconds since 2000 |

#### 0x31/0x32 — Motor/LED Aging Test

**Value (1 byte)**: `0x01`=start; `0x02`=end

---

### 3.8 Control Command (command id 0x07)

L2 version: **0**

| Key | Definition | Direction | Value |
|-----|------|------|-------|
| `0x01` | Photo control | Device→Phone | empty |
| `0x02` | Single-click control | Device→Phone | empty |
| `0x03` | Double-click control | Device→Phone | empty |
| `0x11` | Camera app status request | Phone→Device | `0x00`=foreground; `0x01`=background |

---

### 3.9 Dump Stack Command (command id 0x08)

> Not implemented yet

| Key | Definition |
|-----|------|
| `0x01` | Phone requests the bracelet's assert location info |
| `0x02` | Bracelet returns assert location info |
| `0x03` | Phone requests assert stack info |
| `0x04` | Bracelet reports assert stack info |

---

### 3.10 Test Flash Read Command (command id 0x09)

> Not implemented yet

---

### 3.11 Log Command (command id 0x0a)

L2 version: **0**

| Key | Definition | Direction | Value |
|-----|------|------|-------|
| `0x01` | Enable logging | Phone→Device | empty |
| `0x02` | Disable logging | Phone→Device | empty |
| `0x03` | Log transmission | Device→Phone | 0~499 byte log string |

---

### 3.12 File Transfer Command (command id 0x0b)

L2 version: **0**

#### Overview

The file transfer command provides an application-layer session mechanism on top of the L1 reliable transport, supporting one-way transfer of images, videos, and raw binary data from the phone to the device. The transfer flow is as follows:

```
Phone                                   Device
 │                                        │
 │──── XFER_BEGIN_REQ (0x01) ────────────▶│  negotiate file type, size, chunk size
 │◀─── XFER_BEGIN_RSP (0x02) ────────────│  accept or reject; return actual chunk size
 │                                        │
 │──── XFER_DATA      (0x03) [seq=0] ───▶│  ┐
 │──── XFER_DATA      (0x03) [seq=1] ───▶│  │ continuous transmission, L1 reliable transport guarantees arrival
 │──── XFER_DATA      (0x03) [seq=2] ───▶│  ┘ on sequence error the device sends XFER_ABORT
 │              …                         │
 │──── XFER_END_REQ   (0x05) ───────────▶│  carries the CRC32 of the complete file
 │◀─── XFER_END_RSP   (0x06) ───────────│  validation result
 │                                        │
```

Either side may send XFER_ABORT (0x07) at any time during the session to terminate the transfer immediately. Only one active transfer session is allowed at a time; if the device receives a new XFER_BEGIN_REQ before the current session ends, it should reply with status = `0x01` (device busy).

#### Key List

| Key | Definition | Direction |
|-----|------|------|
| `0x01` | Transfer session setup request | Phone → Device |
| `0x02` | Transfer session setup response | Device → Phone |
| `0x03` | Data block send | Phone → Device |
| `0x05` | Transfer end request | Phone → Device |
| `0x06` | Transfer end response | Device → Phone |
| `0x07` | Transfer abort | Bidirectional |

---

#### 0x01 — Transfer Session Setup Request (XFER_BEGIN_REQ)

**Direction**: Phone → Device | **Value (8 + filename_len bytes)**:

| Field | Size | Description |
|------|------|------|
| file_type | 1 byte | `0x01`=image; `0x02`=video; `0x03`=raw data |
| total_size | 4 bytes | Total file size in bytes (Big-Endian) |
| chunk_size | 2 bytes | Requested bytes per chunk, valid range 1~2048 (Big-Endian) |
| filename_len | 1 byte | Filename length in bytes; `0x00` means no filename follows |
| filename | filename_len bytes | UTF-8 encoded filename, without a null terminator |

> `chunk_size` is the value desired by the phone; the device may reduce it in the response. Both sides use the value in XFER_BEGIN_RSP.

---

#### 0x02 — Transfer Session Setup Response (XFER_BEGIN_RSP)

**Direction**: Device → Phone | **Value (3 bytes)**:

| Field | Size | Description |
|------|------|------|
| status | 1 byte | `0x00`=accepted; `0x01`=device busy; `0x02`=insufficient storage; `0x03`=unsupported file type |
| chunk_size | 2 bytes | Chunk size actually accepted by the device (valid only when status=`0x00`; Big-Endian) |

---

#### 0x03 — Data Block Send (XFER_DATA)

**Direction**: Phone → Device | **Value (2 + data_len bytes)**:

| Field | Size | Description |
|------|------|------|
| seq | 2 bytes | Block sequence number, starting at `0` and increasing monotonically (Big-Endian) |
| data | 1~chunk_size bytes | Block data; the last block may be smaller than chunk_size |

> L1 reliable transport guarantees in-order arrival of data blocks; if the device detects a discontinuity in sequence numbers, it should send XFER_ABORT (reason=`0x01`).

---

#### 0x05 — Transfer End Request (XFER_END_REQ)

**Direction**: Phone → Device | **Value (4 bytes)**:

| Field | Size | Description |
|------|------|------|
| crc32 | 4 bytes | CRC32 checksum of the complete file (polynomial `0xEDB88320`, Big-Endian) |

---

#### 0x06 — Transfer End Response (XFER_END_RSP)

**Direction**: Device → Phone | **Value (2 bytes)**:

| Field | Size | Description |
|------|------|------|
| status | 1 byte | `0x00`=success; `0x01`=CRC32 validation failed; `0x02`=incomplete data |
| error_code | 1 byte | `0x00`=none; `0x01`=write failed; `0x02`=decode failed (valid only when status≠`0x00`) |

---

#### 0x07 — Transfer Abort (XFER_ABORT)

**Direction**: Bidirectional | **Value (1 byte)**:

| Field | Size | Description |
|------|------|------|
| reason | 1 byte | `0x00`=user cancelled; `0x01`=transfer error; `0x02`=timeout |

> The party receiving XFER_ABORT should stop the transfer immediately and release the session resources; no reply is required.

---

### 3.13 BLE Connection Parameter Command (command id 0x0c)

L2 version: **0**

| Key | Definition |
|-----|------|
| `0x01` | Request BLE connection parameters (Phone→Device) |
| `0x02` | Return BLE connection parameters (Device→Phone) |

#### 0x01 — Request BLE Connection Parameters

**Direction**: Phone → Device | **Value**: empty (v-length = 0)

#### 0x02 — Return BLE Connection Parameters

**Direction**: Device → Phone | **Value (7 bytes, Big-Endian)**:

| Field | Size | Description |
|------|------|------|
| conn_interval | 2 bytes | Current connection interval, unit 1.25 ms (e.g. 0x0018 = 24 × 1.25 ms = 30 ms) |
| conn_latency | 2 bytes | Slave latency (Slave Latency) |
| conn_supervision_timeout | 2 bytes | Supervision timeout, unit 10 ms (e.g. 0x01F4 = 500 × 10 ms = 5000 ms) |
| conn_mtu_size | 1 byte | Current ATT MTU size (bytes) |

---



### 3.14 WiFi Provisioning Command (command id 0x0d)

L2 version: **0**

#### Overview

The WiFi provisioning command allows the App to send WiFi credentials (SSID/password) to the device over the established BLE connection, monitor the provisioning status, and, after successful provisioning, obtain the IP and port of the device TCP service for subsequent switching to the WiFi transport channel.

#### Provisioning Interaction Flow

```
App                                     Device
 │                                        │
 │──── WIFI_CONFIG_SET (0x01) ──────────▶│  send SSID / password
 │◀─── WIFI_CONFIG_ACK (0x02) ──────────│  accept or reject (5s timeout)
 │                                        │
 │      (device initiates WiFi connection in background)  │
 │                                        │
 │◀─── WIFI_STATUS     (0x04) ──────────│  proactively report status changes (connecting / connected / failed)
 │                                        │
 │  [optional: no final status received after 30s timeout]  │
 │──── WIFI_STATUS_REQ (0x03) ──────────▶│  actively query current status
 │◀─── WIFI_STATUS     (0x04) ──────────│  return current status
```

- After receiving `WIFI_CONFIG_ACK`, the App starts a 30s status timeout timer;
- After the timeout, send one `WIFI_STATUS_REQ` to query actively, then wait another 10s; if there is still no response, provisioning is considered failed;
- After the device connects successfully, it carries the IP and TCP port in `WIFI_STATUS`, and the App can then initiate a TCP connection.

#### Key List

| Key | Definition | Direction |
|-----|------|------|
| `0x01` | WIFI_CONFIG_SET — send SSID/password | App → Device |
| `0x02` | WIFI_CONFIG_ACK — confirm whether the request is accepted | Device → App |
| `0x03` | WIFI_STATUS_REQ — actively query provisioning status | App → Device |
| `0x04` | WIFI_STATUS — report provisioning status / IP | Device → App |

---

#### 0x01 — WIFI_CONFIG_SET

**Direction**: App → Device | **Value (5 + ssid_len + pwd_len bytes)**:

| Field | Size | Description |
|------|------|------|
| request_id | 2 bytes | Request sequence number (Big-Endian), used to match the ACK; incremented on each provisioning attempt |
| flags | 1 byte | Bit flags: bit0 = `FLAG_SAVE_CREDENTIALS` (1 = save credentials to the device for automatic connection next time) |
| ssid_len | 1 byte | SSID length in UTF-8 encoded bytes (1~32) |
| ssid | ssid_len bytes | SSID, UTF-8 encoded, without a null terminator |
| pwd_len | 1 byte | Password length in UTF-8 encoded bytes (0~64); 0 means an open network with no password |
| password | pwd_len bytes | Password, UTF-8 encoded, without a null terminator |

---

#### 0x02 — WIFI_CONFIG_ACK

**Direction**: Device → App | **Value (4 bytes)**:

| Field | Size | Description |
|------|------|------|
| request_id | 2 bytes | Corresponding request sequence number (Big-Endian) |
| result | 1 byte | `0x00`=accepted; `0x01`=rejected |
| error | 1 byte | Error code (valid only when result=`0x01`; set to `0x00` when accepted) |

**error error codes**:

| Error code | Description |
|--------|------|
| `0x00` | No error |
| `0x01` | Malformed payload |
| `0x02` | Unsupported provisioning mode |
| `0x03` | Invalid SSID |
| `0x04` | Invalid password |
| `0x05` | Device busy (a provisioning session is already in progress) |

---

#### 0x03 — WIFI_STATUS_REQ

**Direction**: App → Device | **Value (2 bytes)**:

| Field | Size | Description |
|------|------|------|
| request_id | 2 bytes | Corresponding request sequence number (Big-Endian) |

> After receiving this command, the device should immediately reply with the current provisioning status (`WIFI_STATUS`), even if the status is still idle.

---

#### 0x04 — WIFI_STATUS

**Direction**: Device → App | **Value (6 + ip_len bytes)**:

| Field | Size | Description |
|------|------|------|
| request_id | 2 bytes | Corresponding request sequence number (Big-Endian) |
| state | 1 byte | Provisioning status code (see the table below) |
| error | 1 byte | Error code (valid only when state=`0x03`; set to `0x00` for other states) |
| ip_len | 1 byte | IP string length in ASCII-encoded bytes; `0x00` when not connected |
| ip | ip_len bytes | Dotted-decimal IP address string (e.g. `"192.168.1.100"`), ASCII encoded, without a null terminator |
| port | 2 bytes | TCP service port number (Big-Endian; default 8783) |

**state status codes**:

| Status code | Description |
|--------|------|
| `0x00` | Idle (no connection initiated) |
| `0x01` | Connecting (connecting to WiFi) |
| `0x02` | Connected (connected; ip/port fields are valid) |
| `0x03` | Failed (connection failed; the error field is valid) |

**error error codes** (when state=`0x03`):

| Error code | Description |
|--------|------|
| `0x00` | No error |
| `0x01` | WiFi authentication failed (wrong SSID/password) |
| `0x02` | WiFi not found (AP out of range) |
| `0x03` | DHCP failed |
| `0x04` | Connection timeout |
| `0x05` | TCP service failed to start |
| `0x06` | Unknown error |

---

## Appendix: Key Data Structure Quick Reference

### L1 Header Byte Layout (Big-Endian)

```
Byte 0:  [7:0]  Magic = 0xAB
Byte 1:  [7:6]  Reserve
         [5]    ERR flag   (1=error)
         [4]    ACK flag   (1=ACK packet)
         [3:0]  Version    (current=0)
Byte 2:  [7:0]  Payload length high byte
Byte 3:  [7:0]  Payload length low byte
Byte 4:  [7:0]  CRC16 high byte
Byte 5:  [7:0]  CRC16 low byte
Byte 6:  [7:0]  Sequence ID high byte
Byte 7:  [7:0]  Sequence ID low byte
```

### CRC16 Calculation Range

Covers **Header[0..3]** (Magic + Byte1 + Payload Length) + the **complete Payload**.

### L2 Header Byte Layout (Big-Endian)

```
Byte 0:  [7:0]  Command ID
Byte 1:  [7:4]  Version
         [3:0]  Reserve
```

### Date Field (16 bits, common to all commands)

```
[15]    Reserve
[14:9]  Year   (0~63, from year 2000)
[8:5]   Month  (1~12)
[4:0]   Day    (1~31)
```
