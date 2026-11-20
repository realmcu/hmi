<#
.SYNOPSIS
  Flash only the app image (do not touch romfs / FDB) — Windows version of flash_app.sh.

.DESCRIPTION
  Default image is bin\bank<N>\app_bank<N>.bin; the flash address is read from
  BANK<N>_APP_ADDR in flash_map.ini. Use -Fw to specify a packed full image (app+romfs).

.EXAMPLE
  .\tools\scripts\flash_app.ps1                 # bank0 + COM5
.EXAMPLE
  .\tools\scripts\flash_app.ps1 -Bank 1 -Port COM7
.EXAMPLE
  .\tools\scripts\flash_app.ps1 -Fw bin\bank0\app_bank0_packed.bin -DryRun
#>

# #requires must come after the help block; see the note in build.ps1.
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
