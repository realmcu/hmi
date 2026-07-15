@echo off
setlocal enabledelayedexpansion

rem  Dashboard gcc download wrapper
rem  Usage: download.bat [COM] [MODE] [USERDATA_FILE USERDATA_ADDR]
rem    MODE in: src_bank0 (default) | src_bank1 | lib_bank0 | lib_bank1

set SCRIPT_DIR=%~dp0
set DOWNLOAD_BAT=%SCRIPT_DIR%..\download\download.bat

rem  Default applies only when run directly; west overrides via commands.py.
set COM=%1
if "%COM%"=="" set COM=COM3

set MODE=%2
if "%MODE%"=="" set MODE=src_bank0

set BIN_DIR=%SCRIPT_DIR%bin\RTL8773E.hmi_dashboard_%MODE%

rem MODE format: <gui>_<bank>  e.g. src_bank0 -> tokens 1=src 2=bank0
rem build_bank (suffix from build.cmake) = the part after last "_" in defconfig name
rem so artifact prefix = dashboard_<bank>_MP-*.bin
for /f "tokens=2 delims=_" %%a in ("%MODE%") do set BANK=%%a

set APP_BIN=
for /f "delims=" %%i in ('dir /b /od "%BIN_DIR%\dashboard_%BANK%_MP-*.bin" 2^>nul') do (
    set APP_BIN=!BIN_DIR!\%%i
)
if not defined APP_BIN (
    echo [ERROR] App bin not found in: %BIN_DIR%
    echo         Expected pattern: dashboard_%BANK%_MP-*.bin
    echo         Build first: west build -m %MODE%
    pause
    exit /b 1
)

call "%DOWNLOAD_BAT%" %COM% "%APP_BIN%" %3 %4

endlocal
