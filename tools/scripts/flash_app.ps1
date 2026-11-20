<#
.SYNOPSIS
  只烧 app 镜像（不动 romfs / FDB）——Windows 版 flash_app.sh。

.DESCRIPTION
  默认镜像 bin\bank<N>\app_bank<N>.bin，烧录地址读 flash_map.ini 的
  BANK<N>_APP_ADDR。打包过的完整镜像（app+romfs）用 -Fw 指定。

.EXAMPLE
  .\tools\scripts\flash_app.ps1                 # bank0 + COM5
.EXAMPLE
  .\tools\scripts\flash_app.ps1 -Bank 1 -Port COM7
.EXAMPLE
  .\tools\scripts\flash_app.ps1 -Fw bin\bank0\app_bank0_packed.bin -DryRun
#>

# #requires 必须排在帮助块之后，见 build.ps1 里的说明。
#requires -Version 5

[CmdletBinding()]
param(
    [ValidateSet(0, 1)]
    [int]$Bank = 0,
    [string]$Port,
    [string]$Fw,
    [string]$Addr,
    [string]$Baud = '3000000',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_common.ps1')

$Port = Get-EbadgePort $Port
if (-not $Fw) { $Fw = Join-Path $AppDir "bin\bank$Bank\app_bank$Bank.bin" }
if (-not $Addr) { $Addr = Get-BankAppAddr $Bank }
$Mpcli = Get-EbadgeMpcli

if (-not (Test-Path $Mpcli)) { throw "找不到 mpcli.exe: $Mpcli" }
if (-not (Test-Path $Fw)) {
    throw "找不到镜像: $Fw`n       先生成: .\tools\scripts\build.ps1 -Bank $Bank"
}
$FwFull = (Resolve-Path $Fw).Path

Write-Host "[app-flash] exe  = $Mpcli" -ForegroundColor Cyan
Write-Host "[app-flash] port = $Port   addr = $Addr   baud = $Baud" -ForegroundColor Cyan
Write-Host "[app-flash] fw   = $FwFull" -ForegroundColor Cyan

Invoke-Mpcli -Mpcli $Mpcli -DryRun:$DryRun -Arguments @(
    '-c', $Port, '-T', 'RTL87X3G', '-M', '5', '-p',
    '-A', $Addr, '-F', $FwFull, '-b', $Baud, '-r', '-u', '-d'
)
