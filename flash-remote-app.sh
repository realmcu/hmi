#!/usr/bin/env bash
# Upload and flash the application through the remote MPCLI server.
#
# Usage:
#   scripts/flash-remote-app.sh
#   scripts/flash-remote-app.sh 127.0.0.1:18790
#   SERVER=127.0.0.1:18790 FW=/path/to/app.bin scripts/flash-remote-app.sh
#   DRY=1 scripts/flash-remote-app.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SERVER="${1:-${SERVER:-127.0.0.1:18790}}"
CLIENT="${CLIENT:-$SCRIPT_DIR/tool/mpcli/remote_flash_client.py}"
PYTHON="${PYTHON:-python3}"
FW="${FW:-$REPO_ROOT/bin/app.bin}"
ADDR="${ADDR:-0x7009E000}"
BAUD="${BAUD:-2000000}"

[ -f "$CLIENT" ] || { echo "[remote-flash] client not found: $CLIENT" >&2; exit 1; }
[ -f "$FW" ] || { echo "[remote-flash] firmware not found: $FW (run scripts/build.sh first)" >&2; exit 1; }
command -v "$PYTHON" >/dev/null 2>&1 || {
  echo "[remote-flash] Python not found: $PYTHON" >&2
  exit 1
}

set -- \
  --server "$SERVER" \
  -T RTL87X3G -M 5 \
  -p -A "$ADDR" -F "$FW" \
  -b "$BAUD" -r -u -d

echo "[remote-flash] server = $SERVER"
echo "[remote-flash] fw     = $FW"
echo "[remote-flash] addr   = $ADDR   baud = $BAUD"

if [ "${DRY:-0}" = "1" ]; then
  printf '[remote-flash][dry-run] %q %q ' "$PYTHON" "$CLIENT"
  printf '%q ' "$@"
  echo
  exit 0
fi

exec "$PYTHON" "$CLIENT" "$@"
