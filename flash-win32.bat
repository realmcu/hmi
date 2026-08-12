@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Flash the RTL87X3G application from native Windows cmd.exe.
rem Usage: flash-win32.bat COM13
rem Optional environment variables: BAUD, FW, ADDR, MPCLI_EXE, DRY, NOPAUSE.

for %%I in ("%~dp0..") do set "REPO_ROOT=%%~fI"

rem A positional COM port takes precedence over the PORT environment variable.
if not "%~1"=="" set "PORT=%~1"

if not defined MPCLI_EXE set "MPCLI_EXE=%~dp0tool\mpcli\mpcli.exe"
if not defined FW set "FW=%REPO_ROOT%\bin\app.bin"
if not defined ADDR set "ADDR=0x7009E000"
if not defined BAUD set "BAUD=2000000"

if not defined PORT (
  echo [flash] Missing COM port. Usage: scripts\flash-win32.bat COMx 1>&2
  goto :fail
)
if not exist "%MPCLI_EXE%" (
  echo [flash] mpcli.exe not found: %MPCLI_EXE% 1>&2
  goto :fail
)
if not exist "%FW%" (
  echo [flash] Firmware not found: %FW% 1>&2
  goto :fail
)

echo [flash] exe  = %MPCLI_EXE%
echo [flash] port = %PORT%   addr = %ADDR%   baud = %BAUD%
echo [flash] fw   = %FW%

pushd "%~dp0tool\mpcli" >nul || goto :fail

if /I "%DRY%"=="1" (
  echo [flash][dry-run] "%MPCLI_EXE%" -c %PORT% -T RTL87X3G -M 5 -p -A %ADDR% -F "%FW%" -b %BAUD% -r -u -d
  set "RC=0"
) else (
  "%MPCLI_EXE%" -c %PORT% -T RTL87X3G -M 5 -p -A %ADDR% -F "%FW%" -b %BAUD% -r -u -d
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
