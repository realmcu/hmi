@echo off
setlocal enabledelayedexpansion

rem ============================================================
rem  Usage:
rem    download.bat [COM] [APP_BIN] [USERDATA_FILE USERDATA_ADDR]
rem
rem  %1  COM port           (default: DEFAULT_COM below)
rem  %2  App bin path       (default: search dashboard gcc output)
rem  %3  Userdata file      (optional)
rem  %4  Userdata address   (required when %3 is given)
rem ============================================================

rem ============================================================
rem  CONFIG: set your COM port here (leave blank to prompt)
rem  Applies only when run directly; west overrides via commands.py.
set DEFAULT_COM=COM3
rem ============================================================

set BAUD=3000000
set EXITCODE=0

set COM=%1
if "%COM%"=="" set COM=%DEFAULT_COM%
if "%COM%"=="" set /p COM=Enter COM port (e.g. COM3):
if "%COM%"=="" ( echo [ERROR] COM port is required. & set EXITCODE=1 & goto :end )

set SCRIPT_DIR=%~dp0
set MPCLI_DIR=%SCRIPT_DIR%mpcli
set FLASH_MAP=%SCRIPT_DIR%..\..\..\..\bin\rtl87x3ep\flash_map_config\16M\flash_16M\flash_map.h

if not exist "%FLASH_MAP%" ( echo [ERROR] flash_map.h not found: %FLASH_MAP% & set EXITCODE=1 & goto :end )

rem --- Parse BANK0/BANK1 APP addresses from flash_map.h ---
set BANK0_APP_ADDR=
set BANK1_APP_ADDR=
for /f "tokens=3" %%A in ('findstr /C:"BANK0_APP_ADDR" "%FLASH_MAP%"') do set BANK0_APP_ADDR=%%A
for /f "tokens=3" %%A in ('findstr /C:"BANK1_APP_ADDR" "%FLASH_MAP%"') do set BANK1_APP_ADDR=%%A
if not defined BANK0_APP_ADDR ( echo [ERROR] Cannot parse BANK0_APP_ADDR from flash_map.h & set EXITCODE=1 & goto :end )
if not defined BANK1_APP_ADDR ( echo [ERROR] Cannot parse BANK1_APP_ADDR from flash_map.h & set EXITCODE=1 & goto :end )

rem --- Resolve app bin path ---
set APP_BIN=%2
if not defined APP_BIN ( echo [ERROR] App bin path required as %%2. & set EXITCODE=1 & goto :end )
if not exist "%APP_BIN%" ( echo [ERROR] App bin not found: %APP_BIN% & set EXITCODE=1 & goto :end )

rem --- Select flash address by bank, inferred from the bin filename ---
rem     A bank1 image is LINKED at BANK1_APP_ADDR and MUST be flashed there;
rem     flashing it to the bank0 address (or vice versa) leaves every absolute
rem     address in the image pointing at the wrong bank -> boots into garbage.
echo "%APP_BIN%" | findstr /I "bank1" >nul
if not errorlevel 1 (
    set APP_ADDR=%BANK1_APP_ADDR%
    set BANK_NAME=bank1
) else (
    set APP_ADDR=%BANK0_APP_ADDR%
    set BANK_NAME=bank0
)

echo [APP]      %APP_BIN%
echo            -^> Bank %BANK_NAME%  Flash %APP_ADDR%  Port: %COM%

pushd "%MPCLI_DIR%"
"%MPCLI_DIR%\mpcli.exe" -c %COM% -p -A %APP_ADDR% -F "%APP_BIN%" -b %BAUD% -M 5 -r -u -d -T RTL87X3EP
set MPCLI_ERR=%ERRORLEVEL%
popd
if %MPCLI_ERR% neq 0 ( echo [ERROR] App download failed. & set EXITCODE=1 & goto :end )

rem --- Optional userdata download ---
if "%3"=="" goto :end
if "%4"=="" ( echo [ERROR] Userdata address ^(%%4^) required when userdata file ^(%%3^) is given. & set EXITCODE=1 & goto :end )

echo [USERDATA] %3
echo            -^> Flash %4  Port: %COM%

pushd "%MPCLI_DIR%"
"%MPCLI_DIR%\mpcli.exe" -c %COM% -p -A %4 -F "%3" -b %BAUD% -M 5 -r -u -d -T RTL87X3EP
set MPCLI_ERR=%ERRORLEVEL%
popd
if %MPCLI_ERR% neq 0 ( echo [ERROR] Userdata download failed. & set EXITCODE=1 & goto :end )

:end
if %EXITCODE%==0 (echo [DONE]) else (echo [FAILED])
pause
endlocal
exit /b %EXITCODE%
