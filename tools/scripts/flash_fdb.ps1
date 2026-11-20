<#
.SYNOPSIS
  Flash the prebuilt resource FDB image (tools\pack\fdb_resource_bin\fdb_prebuilt.bin)
  — Windows version of flash_fdb.sh.

.DESCRIPTION
  Flash address = fdb.kvdb_addr in layout.json; an overlap check is done against the
  other partitions in flash_map.ini, so app/OTA are not overwritten by accident.

  mpcli's -F rejects any single file larger than CONFIG_FLASH_SIZE_MAX (RTL87X3G defaults
  to only 2MB), so the image is sliced by -Split (2MB by default) and flashed at consecutive addresses.

.EXAMPLE
  .\tools\scripts\flash_fdb.ps1                 # default COM5
.EXAMPLE
  .\tools\scripts\flash_fdb.ps1 -Port COM7 -Fdb tools\pack\fdb_resource_bin\fdb_prebuilt.bin
.EXAMPLE
  .\tools\scripts\flash_fdb.ps1 -DryRun
#>

# #requires must come after the help block; see the note in build.ps1.
#requires -Version 5

[CmdletBinding()]
param(
    [string]$Port,
    [string]$Fdb = $env:FW,
    [string]$Addr,
    [string]$Baud = '3000000',
    [string]$Split = '0x200000',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_common.ps1')

$Port = Get-EbadgePort $Port
$Mpcli = Get-EbadgeMpcli
$Layout = Join-Path $AppDir 'tools\pack\layout.json'
if (-not $Fdb) {
    $Fdb = Join-Path $AppDir 'tools\pack\fdb_resource_bin\fdb_prebuilt.bin'
}

if (-not (Test-Path $Mpcli)) { throw "找不到 mpcli.exe: $Mpcli" }
if (-not (Test-Path $Fdb)) {
    throw "找不到镜像: $Fdb`n       先生成: .\tools\scripts\build_fdb.ps1"
}
if (-not (Test-Path $Layout)) { throw "找不到 $Layout" }

# ---- layout and image checks ----
# UTF-8 must be explicit: Windows PowerShell 5.1 decodes Get-Content as ANSI by default,
# which corrupts the Chinese comments in layout.json and makes ConvertFrom-Json fail outright.
$lay = Get-Content -Raw -Encoding UTF8 -Path $Layout | ConvertFrom-Json
$kv = Convert-EbadgeHex $lay.fdb.kvdb_addr
$kvSz = Convert-EbadgeHex $lay.fdb.kvdb_size
$bf = Convert-EbadgeHex $lay.fdb.bf_data_addr
$bfSz = Convert-EbadgeHex $lay.fdb.bf_data_size
if ($bf -ne ($kv + $kvSz)) { throw 'layout.json: bf_data_addr 必须紧接 kvdb' }

if (-not $Addr) { $Addr = $lay.fdb.kvdb_addr }
$addrInt = Convert-EbadgeHex $Addr
$FdbFull = (Resolve-Path $Fdb).Path
$size = (Get-Item $FdbFull).Length
$total = $kvSz + $bfSz

if ($addrInt -ne $kv) {
    throw ("烧录地址 {0} 与 kvdb_addr {1} 不一致" -f (Format-EbadgeHex $addrInt), (Format-EbadgeHex $kv))
}
if ($size -lt $kvSz) {
    throw ("镜像 {0} B 小于 KVDB ({1} B)，目录项不完整" -f $size, $kvSz)
}
if ($size -gt $total) {
    throw ("镜像 {0} B 超过 kvdb+bf 区 {1} B" -f $size, $total)
}
if ($size -lt $total) {
    Write-Host ("[fdb-flash] 尾部 {0} B 未写入（保持擦除态 0xFF，BF 区未启用部分）" -f ($total - $size))
}

Write-Host ("[fdb-flash] 镜像   = {0}" -f $FdbFull)
Write-Host ("[fdb-flash] 大小   = {0} B ({1:N2} MB)" -f $size, ($size / 1048576))
Write-Host ("[fdb-flash] 区域   = {0} .. {1}  (kvdb {2}, bf {3})" -f `
    (Format-EbadgeHex $kv), (Format-EbadgeHex ($kv + $total)), `
    (Format-EbadgeHex $kv), (Format-EbadgeHex $bf))

# overlap check against the other partitions in flash_map.ini
$reg = @{}
foreach ($line in Get-Content -Path (Join-Path $AppDir 'flash_map.ini')) {
    $m = [regex]::Match($line.Trim(), '^(\w+)=(0x[0-9A-Fa-f]+)$')
    if ($m.Success) { $reg[$m.Groups[1].Value] = Convert-EbadgeHex $m.Groups[2].Value }
}
$lo = $kv
$hi = $kv + $total
$bad = @()
foreach ($k in $reg.Keys) {
    if (-not $k.EndsWith('_ADDR')) { continue }
    $v = $reg[$k]
    if ($v -eq 0) { continue }
    $base = $k.Substring(0, $k.Length - 5)
    $sz = 0
    if ($reg.ContainsKey($base + '_SIZE')) { $sz = $reg[$base + '_SIZE'] }
    if ($sz -gt 0 -and $v -lt $hi -and ($v + $sz) -gt $lo) {
        $bad += ("{0} {1}..{2}" -f $base, (Format-EbadgeHex $v), (Format-EbadgeHex ($v + $sz)))
    }
}
if ($bad.Count -gt 0) { throw ('会覆盖其它分区: ' + ($bad -join ', ')) }
Write-Host '[fdb-flash] 重叠检查: 通过' -ForegroundColor Green

# ---- chunked flashing ----
$chunk = Convert-EbadgeHex $Split
if ($chunk -gt 0 -and ($chunk % 4096) -ne 0) {
    throw "分段大小必须 4K 对齐（当前 $chunk）"
}
if ($chunk -le 0 -or $chunk -gt $size) { $chunk = $size }
$n = [int64][math]::Ceiling([double]$size / [double]$chunk)

Write-Host "[fdb-flash] exe   = $Mpcli" -ForegroundColor Cyan
Write-Host "[fdb-flash] port  = $Port   addr = $Addr   baud = $Baud" -ForegroundColor Cyan
Write-Host "[fdb-flash] 分段  = $n x $chunk B (镜像 $size B)" -ForegroundColor Cyan

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('fdb-flash-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $fs = [System.IO.File]::OpenRead($FdbFull)
    try {
        for ($i = [int64]0; $i -lt $n; $i++) {
            $off = $i * $chunk
            $len = [int][math]::Min($chunk, $size - $off)
            $buf = New-Object byte[] $len
            $fs.Position = $off
            $read = 0
            while ($read -lt $len) {
                $r = $fs.Read($buf, $read, $len - $read)
                if ($r -le 0) { throw '读取镜像失败（文件被截断？）' }
                $read += $r
            }

            $addrI = Format-EbadgeHex ($addrInt + $off)
            Write-Host ("[fdb-flash] part {0}/{1}: {2} +0x{3:X}" -f ($i + 1), $n, $addrI, $len)

            if ($DryRun) {
                Write-Host ("[fdb-flash][dry-run] `"$Mpcli`" -c $Port -T RTL87X3G -M 5 -p -A $addrI -F <part$i.bin> -b $Baud -r -u -d")
            }
            else {
                $part = Join-Path $tmp ("part$i.bin")
                [System.IO.File]::WriteAllBytes($part, $buf)
                Invoke-Mpcli -Mpcli $Mpcli -Arguments @(
                    '-c', $Port, '-T', 'RTL87X3G', '-M', '5', '-p',
                    '-A', $addrI, '-F', $part, '-b', $Baud, '-r', '-u', '-d'
                )
            }
        }
    }
    finally { $fs.Dispose() }
}
finally {
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

if (-not $DryRun) { Write-Host '[fdb-flash] 完成' -ForegroundColor Green }
