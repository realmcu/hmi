@echo off
setlocal

set UV4=C:\Keil_v5\UV4\UV4.exe
set SDK=%~dp0..\..\..
set HAL=%~dp0..\..\..\..\hal
set FW=%~dp0..\framework
set LOG=%~dp0pre_env_build.log

echo Building pre-requisite libs... > "%LOG%"

echo [1/6] Building console.lib...
"%UV4%" -rebuild "%FW%\console\console.uvprojx" -t rtl87x3ep -j0 -o "%LOG%_console.txt"
if errorlevel 1 goto :error

echo [2/6] Building sysm.lib...
"%UV4%" -rebuild "%FW%\sysm\sysm.uvprojx" -t rtl87x3ep -j0 -o "%LOG%_sysm.txt"
if errorlevel 1 goto :error

echo [3/6] Building remote.lib...
"%UV4%" -rebuild "%FW%\remote\remote.uvprojx" -t rtl87x3ep -j0 -o "%LOG%_remote.txt"
if errorlevel 1 goto :error

echo [4/6] Building btm.lib...
"%UV4%" -rebuild "%FW%\btm\btm.uvprojx" -t rtl87x3ep -j0 -o "%LOG%_btm.txt"
if errorlevel 1 goto :error

echo [5/6] Building audio.lib...
"%UV4%" -rebuild "%FW%\audio\audio.uvprojx" -t rtl87x3ep -j0 -o "%LOG%_audio.txt"
if errorlevel 1 goto :error

echo [6/6] Building hal_utils.lib...
"%UV4%" -rebuild "%HAL%\proj\hal_lib\rtl87x3ep_hal_lib.uvprojx" -t bb2plus -j0 -o "%LOG%_hal_lib.txt"
if errorlevel 1 goto :error

echo.
echo All libs built successfully.
goto :end

:error
echo.
echo ERROR: Build failed. Check log files in %~dp0
exit /b 1

:end
endlocal
