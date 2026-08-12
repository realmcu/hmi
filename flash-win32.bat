@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem ============================================================================
rem  flash-win32.bat -- 在 Windows 原生 cmd 下用 mpcli.exe 烧录 RTL87X3G 的 App
rem
rem  与 flash-wsl.sh / flash-linux.sh 的关系：
rem    - 参数集完全一致：-p -A 0x7009E000 -b 2000000 -M 5 -r -u -d -T RTL87X3G
rem    - 固件默认 %REPO_ROOT%\bin\app.bin （REPO_ROOT = 本脚本上一级目录）
rem    - COM 口不设默认，必须显式传入，避免误烧到其他串口设备
rem
rem  用法：
rem    scripts\flash-win32.bat COM8
rem    set PORT=COM8 ^&^& scripts\flash-win32.bat
rem    set BAUD=3000000 ^&^& scripts\flash-win32.bat COM8
rem    set DRY=1 ^&^& scripts\flash-win32.bat COM8       :: 干跑，只打印命令
rem    set NOPAUSE=1 ^&^& scripts\flash-win32.bat COM8   :: 结束不 pause（脚本调用用）
rem
rem  可覆盖的环境变量：PORT / BAUD / FW / ADDR / MPCLI_EXE / DRY / NOPAUSE
rem ============================================================================

rem ---- REPO_ROOT = 脚本所在目录的上一级 ----
for %%I in ("%~dp0..") do set "REPO_ROOT=%%~fI"

rem ---- 位置参数优先于环境变量，覆盖 PORT ----
if not "%~1"=="" set "PORT=%~1"

rem ---- 默认值（PORT 不给默认） ----
if not defined MPCLI_EXE set "MPCLI_EXE=%~dp0tool\mpcli\mpcli.exe"
if not defined FW        set "FW=%REPO_ROOT%\bin\app.bin"
if not defined ADDR      set "ADDR=0x7009E000"
if not defined BAUD      set "BAUD=2000000"

rem ---- 检查 ----
if not defined PORT (
  echo [flash] 缺少 COM 口参数。用法： scripts\flash-win32.bat COMx   或   set PORT=COMx 后再运行。 1>&2
  goto :fail
)
if not exist "%MPCLI_EXE%" (
  echo [flash] mpcli.exe 不存在: %MPCLI_EXE% 1>&2
  goto :fail
)
if not exist "%FW%" (
  echo [flash] 固件不存在: %FW%（先 west build 生成 bin\app.bin） 1>&2
  goto :fail
)

echo [flash] exe  = %MPCLI_EXE%
echo [flash] port = %PORT%   addr = %ADDR%   baud = %BAUD%
echo [flash] fw   = %FW%

rem mpcli.exe(frozen) 用 sys.executable 目录定位 fw/、config/，与 CWD 无关；
rem 这里 pushd 到 exe 目录纯粹是和 flash-wsl.sh / tasks.json 的 cwd 保持一致。
pushd "%MPCLI_EXE%\.." >nul || goto :fail

set "MPCLI_ARGS=-c %PORT% -T RTL87X3G -M 5 -p -A %ADDR% -F "%FW%" -b %BAUD% -r -u -d"

if /I "%DRY%"=="1" (
  echo [flash][dry-run] "%MPCLI_EXE%" %MPCLI_ARGS%
  set "RC=0"
) else (
  "%MPCLI_EXE%" %MPCLI_ARGS%
  set "RC=!ERRORLEVEL!"
)

popd >nul
goto :done

:fail
set "RC=1"

:done
if /I not "%NOPAUSE%"=="1" (
  echo.
  pause
)
endlocal & exit /b %RC%
