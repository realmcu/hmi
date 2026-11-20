@echo off
:: flash.bat -- drives mpcli.exe on Windows to flash the RTL87X3G App image.
::
:: Usage:
::   scripts\flash.bat           default COM7
::   scripts\flash.bat COM8      select the download port
::
:: Before flashing, make sure the device is in MP download mode (tie P2_0 to GND, then reset).

setlocal

set MPCLI=D:\mpcli_meta_tool_v4.0.0.6_win\mpcli.exe
set ADDR=0x7009E000
set BAUD=3000000
set PORT=COM7

if not "%1"=="" set PORT=%1

set REPO_ROOT=%~dp0..
set FW=%REPO_ROOT%\bin\app.bin

if not exist "%MPCLI%" (
    echo [flash] mpcli.exe 不存在: %MPCLI% >&2
    exit /b 1
)
if not exist "%FW%" (
    echo [flash] 固件不存在: %FW%  请先 west build 生成 bin\app.bin >&2
    exit /b 1
)

echo [flash] exe  = %MPCLI%
echo [flash] port = %PORT%   addr = %ADDR%   baud = %BAUD%
echo [flash] fw   = %FW%

cd /d "%~dp0..\..\..\..\mpcli_meta_tool_v4.0.0.6_win"
"%MPCLI%" -c %PORT% -T RTL87X3G -M 5 -p -A %ADDR% -F "%FW%" -b %BAUD% -r -u -d
