@echo off
:: monitor.bat —— 在 Windows 下读取 COM 口上的 RTL87X3G 串口 log。
::
:: 关键：open 前先将 RTS/DTR 钉成 False，避免复位芯片。
::
:: 用法：
::   scripts\monitor.bat                  COM9 @ 2000000，持续读到 Ctrl-C
::   scripts\monitor.bat COM9 2000000 5   只读 5 秒后退出

setlocal

set PORT=COM9
set BAUD=2000000
set SECS=0

if not "%1"=="" set PORT=%1
if not "%2"=="" set BAUD=%2
if not "%3"=="" set SECS=%3

:: 用 where 自动找第一个 python.exe
for /f "delims=" %%i in ('where python 2^>nul') do (
    set WIN_PY=%%i
    goto :found
)
echo [monitor] 找不到 python.exe，请确认 Python 已加入 PATH >&2
exit /b 1

:found
echo [monitor] python = %WIN_PY%
echo [monitor] %PORT% @ %BAUD%  (secs=%SECS%, 0=until Ctrl-C)

:: bat 不支持 heredoc，将 Python 脚本写到临时文件后执行
set TMPPY=%TEMP%\rtl_monitor_%RANDOM%.py
(
echo import serial, time, sys
echo port  = sys.argv[1]
echo baud  = int^(sys.argv[2]^)
echo limit = float^(sys.argv[3]^)
echo s = serial.Serial^(^)
echo s.port      = port
echo s.baudrate  = baud
echo s.bytesize  = serial.EIGHTBITS
echo s.parity    = serial.PARITY_NONE
echo s.stopbits  = serial.STOPBITS_ONE
echo s.rtscts    = False
echo s.dsrdtr    = False
echo s.xonxoff   = False
echo s.timeout   = 0.1
echo s.rts       = False
echo s.dtr       = False
echo try:
echo     s.open^(^)
echo     s.rts = False
echo     s.dtr = False
echo except Exception as e:
echo     sys.stderr.write^("OPEN_FAIL: %%s\n" %% e^)
echo     sys.exit^(1^)
echo sys.stderr.write^("[OPEN_OK %%s @ %%d]\n" %% ^(port, baud^)^)
echo sys.stderr.flush^(^)
echo t0 = time.time^(^)
echo out = sys.stdout
echo try:
echo     while True:
echo         if limit and ^(time.time^(^) - t0^) ^>= limit:
echo             break
echo         n = s.in_waiting
echo         d = s.read^(n if n else 1^)
echo         if d:
echo             out.write^(d.decode^("latin-1"^)^)
echo             out.flush^(^)
echo except KeyboardInterrupt:
echo     pass
echo finally:
echo     try:
echo         s.close^(^)
echo     except Exception:
echo         pass
echo     sys.stderr.write^("\n[CLOSED %%s]\n" %% port^)
) > "%TMPPY%"

"%WIN_PY%" "%TMPPY%" %PORT% %BAUD% %SECS%
del "%TMPPY%" 2>nul
