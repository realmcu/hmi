#!/usr/bin/env bash
#
# monitor.sh —— 在 WSL2 里读取 Windows COM 口上的 RTL87X3G 串口 log。
#
# 原理：WSL 调用 Windows 的 python.exe（原生进程，能访问 COM 口），
#       程序经 stdin(heredoc) 喂入，无需在 Windows 侧落脚本文件。
#
# 关键：复刻 serial_term.py 的安全打开序列——空构造后先把 rts/dtr 钉成 False
#       再 open()。RTL87X3G 的 RTS 是“低有效复位线”，普通工具一打开就会复位芯片，
#       本脚本全程保持 RTS/DTR deassert，绝不复位。
#
# 用法：
#   scripts/monitor.sh                       # COM9 @ 2000000，一直读到 Ctrl-C
#   scripts/monitor.sh COM9 2000000 5        # 只读 5 秒后退出（适合脚本/抓快照）
#   scripts/monitor.sh COM9 | tee run.log    # 同时存盘
#   PORT=COM9 BAUD=2000000 scripts/monitor.sh
#
set -euo pipefail

PORT="${1:-${PORT:-COM9}}"
BAUD="${2:-${BAUD:-2000000}}"
SECS="${3:-0}"   # 0 = 一直读到 Ctrl-C
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
limit = float(sys.argv[3])      # 0 => 无限

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
s.rts       = False             # open 前就钉成 deassert
s.dtr       = False
try:
    s.open()
    s.rts = False               # 双保险
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
