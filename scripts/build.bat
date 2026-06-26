@echo off
:: build.bat —— 在 Windows 下调用 west build 编译 RTL87X3G 应用。
::
:: 用法：
::   scripts\build.bat          增量编译
::   scripts\build.bat -p       全量重编（pristine）

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
