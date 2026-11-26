#!/usr/bin/env bash
# Detect USER_DATA1(0xFFFE), resolve its address and flash it with mpcli on macOS.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DIRECT="$SCRIPT_DIR/flash-macos-direct-addr.sh"

FW="${1:-}"
PORT="${2:-${PORT:-}}"

die() { echo "[flash-macos-user-data1] $*" >&2; exit 1; }

[ -n "$FW" ] || die "usage: $(basename "$0") <USER_DATA1 image> [port]"
[ -f "$FW" ] || die "file not found: $FW"
[[ "$PORT" =~ ^0[xX] ]] && die "address is selected from Image ID; the second argument is the port"
command -v python3 >/dev/null 2>&1 || die "python3 is required to identify Image ID"
[ -x "$DIRECT" ] || die "missing executable: $DIRECT"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/macos-user-data1.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
IMAGE="$tmp/user_data1.bin"

if ! ADDR=$(python3 - "$FW" "$IMAGE" "$ROOT/applications/flash_map.h" <<'PY'
import re
import struct
import sys

source, output, flash_map = sys.argv[1:4]
data = open(source, "rb").read()
mp_id = None
image = data

if data[:2] == b"\x01\x00":
    tlvs = {}
    offset = 0
    while offset + 2 <= 512:
        key = struct.unpack_from("<H", data, offset)[0]
        if key == 0:
            break
        if offset + 3 > 512:
            raise SystemExit("MP header is truncated")
        length = data[offset + 2]
        if offset + 3 + length > 512:
            raise SystemExit("MP TLV exceeds the 512-byte header")
        tlvs[key] = data[offset + 3:offset + 3 + length]
        offset += 3 + length
    if 0x12 not in tlvs:
        raise SystemExit("MP header does not contain an Image ID")
    if 0x14 not in tlvs:
        raise SystemExit("MP header does not contain ImageSize")
    mp_id = int.from_bytes(tlvs[0x12], "little")
    mp_size = int.from_bytes(tlvs[0x14], "little")
    if mp_size != len(data) - 512:
        raise SystemExit("MP ImageSize does not match the payload length")
    image = data[512:]

if len(image) < 0x400 or image[:6] != b"IMGHDR":
    raise SystemExit("无法识别 Image ID：缺少 IMGHDR")

ctrl = 196
ic_type = image[ctrl + 4]
image_id = struct.unpack_from("<H", image, ctrl + 6)[0]
payload_len = struct.unpack_from("<I", image, ctrl + 12)[0]
if payload_len + 0x400 != len(image):
    raise SystemExit("IMG payload length does not match the file size")
if ic_type != 0x13:
    raise SystemExit(f"unsupported IC type: 0x{ic_type:02x}")
if mp_id is not None and mp_id != image_id:
    raise SystemExit(
        f"MP Image ID 0x{mp_id:04x} does not match IMG Image ID 0x{image_id:04x}"
    )
if image_id != 0xFFFE:
    raise SystemExit(
        f"unsupported Image ID 0x{image_id:04x}; this script only accepts USER_DATA1(0xFFFE)"
    )

text = open(flash_map, encoding="utf-8").read()

def define(name):
    match = re.search(rf"^\s*#define\s+{name}\s+(0x[0-9A-Fa-f]+)", text, re.M)
    if not match:
        raise SystemExit(f"missing {name} in flash_map.h")
    return int(match.group(1), 16)

address = define("USER_DATA1_ADDR")
capacity = define("USER_DATA1_SIZE")
if len(image) > capacity:
    raise SystemExit(f"USER_DATA1 image {len(image)} exceeds capacity {capacity}")

open(output, "wb").write(image)
print(f"0x{address:x}")
PY
); then
  die "Image ID detection failed; flashing aborted"
fi

echo "[flash-macos-user-data1] detected Image ID 0xFFFE (USER_DATA1)"
echo "[flash-macos-user-data1] target address $ADDR"

DRY="${DRY:-0}" BAUD="${BAUD:-2000000}" MPCLI="${MPCLI:-}" \
  "$DIRECT" "$IMAGE" "$ADDR" "$PORT"
