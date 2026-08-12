@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Flash the bundled runtime firmware from native Windows cmd.exe.
rem
rem Unlike flash-win32.bat (which flashes a single App image at 0x7009E000),
rem this script flashes the whole runtime set (boot patch / upperstack /
rem OTA header / stack & sys patch / DSP sys+app+cfg / SYSTEM & APP Config,
rem 11 images) via mpcli --flash-runtime.
rem
rem The runtime does NOT include the APP, so the full sequence is:
rem   1. scripts\flash-runtime-win32.bat COM13   :: runtime first
rem   2. scripts\flash-win32.bat COM13           :: then bin\app.bin
rem
rem Usage: flash-runtime-win32.bat COM13
rem Optional environment variables: BAUD, CHIP, MPCLI_EXE, DRY, NOPAUSE.

rem A positional COM port takes precedence over the PORT environment variable.
if not "%~1"=="" set "PORT=%~1"

if not defined MPCLI_EXE set "MPCLI_EXE=%~dp0tool\mpcli\mpcli.exe"
rem CHIP is a directory name under tool\mpcli\fw\runtime_system_bin\
rem (RTL8773G / RTL8783G / RTL87X3EP).
if not defined CHIP set "CHIP=RTL8773G"
if not defined BAUD set "BAUD=2000000"

if not defined PORT (
  echo [flash-runtime] Missing COM port. Usage: scripts\flash-runtime-win32.bat COMx 1>&2
  goto :fail
)
if not exist "%MPCLI_EXE%" (
  echo [flash-runtime] mpcli.exe not found: %MPCLI_EXE% 1>&2
  goto :fail
)
if not exist "%~dp0tool\mpcli\fw\runtime_system_bin\%CHIP%\" (
  echo [flash-runtime] Chip dir not found: fw\runtime_system_bin\%CHIP% 1>&2
  echo [flash-runtime] Available: 1>&2
  dir /b /ad "%~dp0tool\mpcli\fw\runtime_system_bin" 1>&2
  goto :fail
)

echo [flash-runtime] exe  = %MPCLI_EXE%
echo [flash-runtime] port = %PORT%   chip = %CHIP%   baud = %BAUD%

pushd "%~dp0tool\mpcli" >nul || goto :fail

rem -u -d must be passed explicitly: mpcli --flash-runtime only injects
rem -M 5 -r -b 3000000, and SYSTEM_Config lands in the protected OEM_CFG
rem region (0x70002000), which cannot be written without -u.
rem -T is derived from CHIP (RTL8773G -> RTL87X3G), so it is not passed.
if /I "%DRY%"=="1" (
  echo [flash-runtime][dry-run] "%MPCLI_EXE%" --flash-runtime %CHIP% -c %PORT% -u -d -b %BAUD%
  set "RC=0"
) else (
  "%MPCLI_EXE%" --flash-runtime %CHIP% -c %PORT% -u -d -b %BAUD%
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
