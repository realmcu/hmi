@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Windows counterpart of build.sh.
rem
rem Usage:
rem   scripts\build.bat
rem   scripts\build.bat -p
rem   scripts\build.bat --pristine
rem   set BOARD=rtl87x3g_evb && scripts\build.bat
rem   set DRY=1 && scripts\build.bat

for %%I in ("%~dp0..") do set "REPO_ROOT=%%~fI"

if not defined BOARD set "BOARD=rtl87x3g_evb"
set "PRISTINE="

:parse_args
if "%~1"=="" goto :run
if /I "%~1"=="-p" (
  set "PRISTINE=-p"
  shift
  goto :parse_args
)
if /I "%~1"=="--pristine" (
  set "PRISTINE=-p"
  shift
  goto :parse_args
)

echo [build] Unknown argument: %~1 1>&2
exit /b 1

:run
if defined PRISTINE (
  echo [build] board = %BOARD%   mode = pristine
) else (
  echo [build] board = %BOARD%
)

pushd "%REPO_ROOT%" >nul || (
  echo [build] Cannot enter repository root: %REPO_ROOT% 1>&2
  exit /b 1
)

if /I "%DRY%"=="1" (
  echo [build][dry-run] west build -b "%BOARD%" %PRISTINE%
  set "RC=0"
) else (
  west build -b "%BOARD%" %PRISTINE%
  set "RC=!ERRORLEVEL!"
)

popd >nul
endlocal & exit /b %RC%
