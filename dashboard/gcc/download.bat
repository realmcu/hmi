@echo off
setlocal enabledelayedexpansion

rem  Dashboard gcc download wrapper
rem  Usage: download.bat [COM] [USERDATA_FILE USERDATA_ADDR]

set SCRIPT_DIR=%~dp0
set DOWNLOAD_BAT=%SCRIPT_DIR%..\..\download\download.bat
set BIN_DIR=%SCRIPT_DIR%bin\RTL8773E.hmi_dashboard_src

set COM=%1
if "%COM%"=="" set COM=COM3

rem --- find app MP bin ---
set APP_BIN=
for /f "delims=" %%i in ('dir /b /od "%BIN_DIR%\honeygui_src_MP-*.bin" 2^>nul') do (
    set APP_BIN=!BIN_DIR!\%%i
)
if not defined APP_BIN (
    echo [ERROR] App bin not found in: %BIN_DIR%
    echo         Build the project first.
    pause
    exit /b 1
)

call "%DOWNLOAD_BAT%" %COM% "%APP_BIN%" %2 %3

endlocal
