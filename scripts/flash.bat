@echo off
:: flash.bat —— 在 Windows 下驱动 mpcli.exe 烧录 RTL87X3G 的 App 镜像。
::
:: 用法：
::   scripts\flash.bat           默认 COM7
::   scripts\flash.bat COM8      指定下载口
::
:: 烧录前请确认设备已进入 MP 下载模式（P2_0 接 GND 后复位）。

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
