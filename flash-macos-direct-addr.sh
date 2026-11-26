#!/usr/bin/env bash
# Flash an arbitrary file to an explicit address with mpcli on macOS.
# The caller must provide both the file and the 4 KB-aligned flash address.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_DIR="$SCRIPT_DIR/tool/mpcli"
SECTOR=4096

FW="${1:-}"
ADDR="${2:-}"
PORT="${3:-}"
BAUD="${BAUD:-2000000}"
MPCLI="${MPCLI:-$TOOL_DIR/mpcli-macos}"

die() { echo "[flash-macos-direct] $*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || {
  echo "[flash-macos-direct] warning: this script targets macOS, current is $(uname -s)" >&2
}

[ -n "$FW" ] || die "usage: $(basename "$0") <file> <address> [port]"
[ -n "$ADDR" ] || die "an explicit flash address is required"
[[ "$ADDR" =~ ^0[xX][0-9a-fA-F]+$ ]] || die "bad address '$ADDR' (expected 0x-prefixed hex)"
[ $(( $((ADDR)) % SECTOR )) -eq 0 ] || die "address $ADDR is not 4KB aligned"
[ -f "$FW" ] || die "file not found: $FW"
[ -f "$MPCLI" ] || die "mpcli not found: $MPCLI"
[ -x "$MPCLI" ] || die "mpcli is not executable: $MPCLI"

FW="$(cd "$(dirname "$FW")" && pwd)/$(basename "$FW")"

usb_ports() {
  for port in /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* \
              /dev/cu.usbmodem*; do
    [ -e "$port" ] && printf '%s\n' "$port"
  done | sort -u
  return 0
}

if [ -z "$PORT" ]; then
  ports="$(usb_ports)"
  if [ -z "$ports" ]; then
    if [ "${DRY:-0}" = "1" ]; then
      PORT="/dev/cu.usbserial-dryrun"
    else
      die "no USB serial port detected"
    fi
  else
    count="$(printf '%s\n' "$ports" | wc -l | tr -d ' ')"
    [ "$count" = "1" ] || die "multiple serial ports detected; pass [port] explicitly"
    PORT="$(printf '%s\n' "$ports" | head -1)"
    echo "[flash-macos-direct] selected serial port $PORT"
  fi
fi

FW_SIZE="$(wc -c < "$FW" | tr -d ' ')"
echo "[flash-macos-direct] mpcli = $MPCLI"
echo "[flash-macos-direct] port  = $PORT   addr = $ADDR   baud = $BAUD"
echo "[flash-macos-direct] fw    = $FW ($FW_SIZE bytes)"

if [ -z "${MPCLI_LOG_DIR:-}" ] && [ ! -w "$(dirname "$MPCLI")" ]; then
  MPCLI_LOG_DIR="$SCRIPT_DIR/log"
  mkdir -p "$MPCLI_LOG_DIR"
  echo "[flash-macos-direct] mpcli directory is not writable; MPCLI_LOG_DIR=$MPCLI_LOG_DIR"
fi
export MPCLI_LOG_DIR

set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW" -b "$BAUD" -r -u -d

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash-macos-direct][dry-run] %q ' "$MPCLI"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI" "$@"
