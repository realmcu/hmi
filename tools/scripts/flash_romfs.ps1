<#
.SYNOPSIS
  只烧 romfs（壁纸/UI 资源），不动 app ——Windows 版 flash_romfs.sh。

.DESCRIPTION
  需要先执行: .\tools\scripts\build.ps1 -Bank <N> -Pack
  生成的 bin\bank<N>\app_romfs_bank<N>_reloc.bin 已按当前偏移重定位。

  烧之前校验：
    - ROMFS_OFFSET 与挂载地址按 4KB 对齐（否则擦除会波及 app 所在扇区）
    - reloc 文件首项目录指针 == app 基址 + 当前偏移（否则是旧包）

.EXAMPLE
  .\tools\scripts\flash_romfs.ps1               # bank0 + COM5
.EXAMPLE
  .\tools\scripts\flash_romfs.ps1 -Bank 1 -Port COM7
.EXAMPLE
  .\tools\scripts\flash_romfs.ps1 -DryRun
#>

# #requires 必须排在帮助块之后，见 build.ps1 里的说明。
#requires -Version 5

[CmdletBinding()]
param(
    [ValidateSet(0, 1)]
    [int]$Bank = 0,
    [string]$Port,
    [string]$Baud = '3000000',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_common.ps1')

$Port = Get-EbadgePort $Port
$Mpcli = Get-EbadgeMpcli
# romfs_layout.h 可能在 src\ 或 src\user\（Designer 新导出布局）。
$LayoutH = Join-Path $AppDir 'app\designer\src\romfs_layout.h'
if (-not (Test-Path $LayoutH)) {
    $userLayout = Join-Path $AppDir 'app\designer\src\user\romfs_layout.h'
    if (Test-Path $userLayout) { $LayoutH = $userLayout }
}
$Romfs = Join-Path $AppDir "bin\bank$Bank\app_romfs_bank${Bank}_reloc.bin"

if (-not (Test-Path $Mpcli)) { throw "找不到 mpcli.exe: $Mpcli" }
if (-not (Test-Path $Romfs)) {
    throw "找不到 $Romfs`n       先执行: .\tools\scripts\build.ps1 -Bank $Bank -Pack"
}
if (-not (Test-Path $LayoutH)) { throw "找不到 romfs_layout.h" }

$m = [regex]::Match((Get-Content -Raw -Encoding UTF8 -Path $LayoutH), '#define\s+ROMFS_OFFSET\s+(0x[0-9A-Fa-f]+)')
if (-not $m.Success) { throw 'romfs_layout.h 里没有 ROMFS_OFFSET' }
$offset = Convert-EbadgeHex $m.Groups[1].Value

$appAddr = Convert-EbadgeHex (Get-BankAppAddr $Bank)
$mount = $appAddr + $offset
$RomfsFull = (Resolve-Path $Romfs).Path
$size = (Get-Item $RomfsFull).Length

# ---- 校验 ----
if (($offset % 4096) -ne 0) {
    throw ("ROMFS_OFFSET={0} 未按 4KB 对齐 —— 烧它会擦掉 app 尾部。`n       重新打包: .\tools\scripts\build.ps1 -Bank $Bank -Pack" -f (Format-EbadgeHex $offset))
}
if (($mount % 4096) -ne 0) {
    throw ("挂载地址 {0} 未按 4KB 对齐" -f (Format-EbadgeHex $mount))
}

$head = New-Object byte[] 16
$fs = [System.IO.File]::OpenRead($RomfsFull)
try { [void]$fs.Read($head, 0, 16) } finally { $fs.Dispose() }
$typ = [BitConverter]::ToUInt32($head, 0)
$nameP = [BitConverter]::ToUInt32($head, 4)
$nchild = [BitConverter]::ToUInt32($head, 12)

if ($typ -ne 1) {
    throw ("{0} 不是 romfs 目录（首项 type={1}）" -f $RomfsFull, $typ)
}
if ([int64]$nameP -ne ($mount + 16)) {
    throw ("{0} 的目录指针指向 {1}，与本次挂载地址 {2} 不符。`n       说明它是按别的偏移打出来的，重新执行: .\tools\scripts\build.ps1 -Bank $Bank -Pack" -f `
        $RomfsFull, (Format-EbadgeHex $nameP), (Format-EbadgeHex $mount))
}

$eraseLo = [int64][math]::Floor($mount / 4096) * 4096
$eraseHi = [int64][math]::Ceiling(($mount + $size) / 4096) * 4096

Write-Host "[romfs] 镜像   = $RomfsFull" -ForegroundColor Cyan
Write-Host ("[romfs] 大小   = {0} B ({1:N2} MB)" -f $size, ($size / 1048576)) -ForegroundColor Cyan
Write-Host ("[romfs] app基址= {0}" -f (Format-EbadgeHex $appAddr)) -ForegroundColor Cyan
Write-Host ("[romfs] 偏移   = {0} (4KB 对齐 OK)" -f (Format-EbadgeHex $offset)) -ForegroundColor Cyan
Write-Host ("[romfs] 落点   = {0} .. {1}" -f (Format-EbadgeHex $mount), (Format-EbadgeHex ($mount + $size))) -ForegroundColor Cyan
Write-Host ("[romfs] 擦除区 = {0} .. {1} ({2} 个 4KB 扇区)" -f `
    (Format-EbadgeHex $eraseLo), (Format-EbadgeHex $eraseHi), (($eraseHi - $eraseLo) / 4096)) -ForegroundColor Cyan
Write-Host ("[romfs] 目录项 = {0} 个，首项指针 {1} OK" -f $nchild, (Format-EbadgeHex $nameP)) -ForegroundColor Green

Invoke-Mpcli -Mpcli $Mpcli -DryRun:$DryRun -Arguments @(
    '-c', $Port, '-T', 'RTL87X3G', '-M', '5', '-p',
    '-A', (Format-EbadgeHex $mount), '-F', $RomfsFull, '-b', $Baud, '-r', '-u', '-d'
)
