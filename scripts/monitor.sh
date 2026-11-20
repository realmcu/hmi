#!/usr/bin/env bash
#
# monitor.sh -- read the RTL87X3G serial log from a Windows COM port in WSL2.
#
# How it works: WSL invokes Windows python.exe (a native process that can access the COM port),
#       and the program is fed via stdin (heredoc), with no script file written on the Windows side.
#
# Key: replicate serial_term.py's safe open sequence -- construct empty, pin rts/dtr to False,
#       then open(). On the RTL87X3G, RTS is an active-low reset line, so ordinary tools reset the chip on open;
#       this script keeps RTS/DTR deasserted throughout and never resets the chip.
#
# Usage:
#   scripts/monitor.sh                       # COM9 @ 2000000, read until Ctrl-C
#   scripts/monitor.sh COM9 2000000 5        # read for 5 seconds then exit (good for scripts/snapshots)
#   scripts/monitor.sh COM9 | tee run.log    # also save to disk
#   PORT=COM9 BAUD=2000000 scripts/monitor.sh
#
set -euo pipefail

PORT="${1:-${PORT:-COM9}}"
BAUD="${2:-${BAUD:-2000000}}"
SECS="${3:-0}"   # 0 = read until Ctrl-C
if [ -z "${WIN_PY:-}" ]; then
  WIN_PY="$(where.exe python 2>/dev/null | head -1 | tr -d '\r')" || true
fi
[ -n "$WIN_PY" ] && [ -f "$WIN_PY" ] || {
  echo "[monitor] 找不到 Windows python.exe，请手动设置: WIN_PY=/mnt/c/...python.exe" >&2
  exit 1
}

echo "[monitor] $PORT @ $BAUD  (secs=$SECS, 0=until Ctrl-C)" >&2

exec "$WIN_PY" - "$PORT" "$BAUD" "$SECS" <<'PY'
import serial, time, sys

port  = sys.argv[1]
baud  = int(sys.argv[2])
limit = float(sys.argv[3])      # 0 => unlimited

s = serial.Serial()
s.port      = port
s.baudrate  = baud
s.bytesize  = serial.EIGHTBITS
s.parity    = serial.PARITY_NONE
s.stopbits  = serial.STOPBITS_ONE
s.rtscts    = False
s.dsrdtr    = False
s.xonxoff   = False
s.timeout   = 0.1
s.rts       = False             # pinned to deassert before open
s.dtr       = False
try:
    s.open()
    s.rts = False               # extra safety
    s.dtr = False
except Exception as e:
    sys.stderr.write("OPEN_FAIL: %s\n" % e)
    sys.exit(1)

sys.stderr.write("[OPEN_OK %s @ %d]\n" % (port, baud))
sys.stderr.flush()

t0 = time.time()
out = sys.stdout
try:
    while True:
        if limit and (time.time() - t0) >= limit:
            break
        n = s.in_waiting
        d = s.read(n if n else 1)
        if d:
            out.write(d.decode("latin-1"))
            out.flush()
except KeyboardInterrupt:
    pass
finally:
    try:
        s.close()
    except Exception:
        pass
    sys.stderr.write("\n[CLOSED %s]\n" % port)
PY
