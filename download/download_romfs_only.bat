@echo off
setlocal enabledelayedexpansion

rem ============================================================
rem  Usage:
rem    download_romfs_only.bat [COM] [ROMFS_BIN] [ROMFS_ADDR]
rem
rem  %1  COM port      (default: DEFAULT_COM below)
rem  %2  ROMFS bin path (default: ..\src\application\designer\build\app_romfs.bin)
rem  %3  ROMFS address  (default: DEFAULT_ROMFS_ADDR below) -- the RUNTIME
rem      mount address used by gui_vfs_mount_romfs(), i.e.
rem      USER_DATA1_ADDR + IMG_HDR_SIZE from the SDK flash_map.h.
rem
rem  ROMFS_ADDR itself is NOT flash-sector aligned (it sits IMG_HDR_SIZE
rem  bytes into the USER_DATA1 partition). mpcli's auto-erase (triggered by
rem  -p) rounds an unaligned -A UP to the next sector boundary, leaving the
rem  leading IMG_HDR_SIZE bytes of the target range unerased -> flash verify
rem  fails ("status 52"). To avoid this we prepend IMG_HDR_SIZE zero-padding
rem  bytes to the image and flash the combined file at the sector-aligned
rem  partition base (ROMFS_ADDR - IMG_HDR_SIZE) instead. The padding bytes
rem  land in the header region the firmware never reads (mount skips them
rem  via base_offset), so this changes nothing at runtime.
rem ============================================================

rem ============================================================
rem  CONFIG: set your COM port and romfs address here
set DEFAULT_COM=COM3
rem  Must match romfsBaseAddr in src\application\designer\project.json
rem  (= USER_DATA1_ADDR + IMG_HDR_SIZE from the SDK flash_map.h)
set DEFAULT_ROMFS_ADDR=0x240f400
set IMG_HDR_SIZE=0x400
rem ============================================================

set BAUD=3000000
set EXITCODE=0

set COM=%1
if "%COM%"=="" set COM=%DEFAULT_COM%
if "%COM%"=="" set /p COM=Enter COM port (e.g. COM3):
if "%COM%"=="" ( echo [ERROR] COM port is required. & set EXITCODE=1 & goto :end )

set SCRIPT_DIR=%~dp0
set MPCLI_DIR=%SCRIPT_DIR%mpcli

set ROMFS_BIN=%2
if "%ROMFS_BIN%"=="" set ROMFS_BIN=%SCRIPT_DIR%..\src\application\designer\build\app_romfs.bin
if not exist "%ROMFS_BIN%" ( echo [ERROR] Romfs bin not found: %ROMFS_BIN% & set EXITCODE=1 & goto :end )

set ROMFS_ADDR=%3
if "%ROMFS_ADDR%"=="" set ROMFS_ADDR=%DEFAULT_ROMFS_ADDR%

rem --- Compute the sector-aligned flash base address = ROMFS_ADDR - IMG_HDR_SIZE ---
set FLASH_ADDR=
for /f "delims=" %%A in ('powershell -NoProfile -Command "'0x{0:X}' -f ([int64]'%ROMFS_ADDR%' - [int64]'%IMG_HDR_SIZE%')"') do set FLASH_ADDR=%%A
if not defined FLASH_ADDR ( echo [ERROR] Failed to compute aligned flash address. & set EXITCODE=1 & goto :end )

rem --- Build a padded image: IMG_HDR_SIZE zero bytes + romfs content ---
set PAD_FILE=%TEMP%\hmi_dashboard_romfs_pad.bin
set PADDED_FILE=%TEMP%\hmi_dashboard_romfs_padded.bin
del /f /q "%PAD_FILE%" "%PADDED_FILE%" >nul 2>&1

for /f "delims=" %%S in ('powershell -NoProfile -Command "[int64]'%IMG_HDR_SIZE%'"') do set PAD_SIZE=%%S
fsutil file createnew "%PAD_FILE%" %PAD_SIZE% >nul
if not exist "%PAD_FILE%" ( echo [ERROR] Failed to create header padding file. & set EXITCODE=1 & goto :end )

copy /b "%PAD_FILE%"+"%ROMFS_BIN%" "%PADDED_FILE%" >nul
if not exist "%PADDED_FILE%" ( echo [ERROR] Failed to build padded romfs image. & set EXITCODE=1 & goto :end )

echo [ROMFS]    %ROMFS_BIN%
echo            -^> Mount addr %ROMFS_ADDR%  (flash base %FLASH_ADDR%, +%IMG_HDR_SIZE% header pad)  Port: %COM%

pushd "%MPCLI_DIR%"
"%MPCLI_DIR%\mpcli.exe" -c %COM% -p -A %FLASH_ADDR% -F "%PADDED_FILE%" -b %BAUD% -M 5 -r -u -d -T RTL87X3EP
set MPCLI_ERR=%ERRORLEVEL%
popd

del /f /q "%PAD_FILE%" "%PADDED_FILE%" >nul 2>&1

if %MPCLI_ERR% neq 0 ( echo [ERROR] Romfs download failed. & set EXITCODE=1 & goto :end )

:end
if %EXITCODE%==0 (echo [DONE]) else (echo [FAILED])
pause
endlocal
exit /b %EXITCODE%
