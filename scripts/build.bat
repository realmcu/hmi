@echo off
:: build.bat -- build the RTL87X3G application with west build on Windows.
::
:: Usage:
::   scripts\build.bat          incremental build
::   scripts\build.bat -p       full rebuild (pristine)

setlocal

set BOARD=rtl87x3g_evb
set PRISTINE=

if "%1"=="-p" set PRISTINE=-p
if "%1"=="--pristine" set PRISTINE=-p

set REPO_ROOT=%~dp0..

echo [build] board = %BOARD%
if defined PRISTINE echo [build] mode  = pristine

cd /d "%REPO_ROOT%"
west build -b %BOARD% %PRISTINE%
