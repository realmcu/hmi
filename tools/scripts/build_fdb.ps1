<#
.SYNOPSIS
  Build the prebuilt wallpaper FDB image (tools\pack\fdb_resource_bin\fdb_prebuilt.bin)
  — Windows version of build_fdb.sh.

.DESCRIPTION
  Assets come by default from tools\pack\fdb_resource_bin (tracked with the project):
    wp-*.bin / pre-*.bin / wp-tear.bin / tear_mask.bin / tear_part.bin /
    wl-video.avi
  If that directory has no wp-1.bin, fall back to the HoneyGUI Designer build tree
  app\designer\build\assets\images.

  Groups are written out explicitly (instead of pack.py --assets-dir) because the tear group needs 4 slots,
  and automatic collection skips wp-tear.bin and .avi by default.

  After building, pack.py reads the image back to verify it (fdb_host_verify) and errors out on failure.
  To flash it use tools\scripts\flash_fdb.ps1.

.EXAMPLE
  .\tools\scripts\build_fdb.ps1
.EXAMPLE
  .\tools\scripts\build_fdb.ps1 -AssetsDir D:\assets\images -NoVideo
#>

# #requires must come after the help block; see the note in build.ps1.
#requires -Version 5

[CmdletBinding()]
param(
    [string]$AssetsDir = $env:ASSETS_DIR,
    [string]$Out = $env:OUT,
    [switch]$NoVideo
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_common.ps1')

$Python = Get-EbadgePython
$withVideo = (-not $NoVideo) -and ($env:WITH_VIDEO -ne '0')

if (-not $Out) {
    $Out = Join-Path $AppDir 'tools\pack\fdb_resource_bin\fdb_prebuilt.bin'
}
if (-not $AssetsDir) {
    foreach ($d in @((Join-Path $AppDir 'tools\pack\fdb_resource_bin'),
                     (Join-Path $AppDir 'app\designer\build\assets\images'))) {
        if (Test-Path (Join-Path $d 'wp-1.bin')) { $AssetsDir = $d; break }
    }
    if (-not $AssetsDir) {
        $AssetsDir = Join-Path $AppDir 'tools\pack\fdb_resource_bin'
    }
}
if (-not (Test-Path $AssetsDir)) {
    throw "素材目录不存在: $AssetsDir`n       用 -AssetsDir 或 ASSETS_DIR 指定 designer 导出的 assets\images"
}

$required = @(
    'wp-1.bin', 'pre-1.bin',
    'wp-2.bin', 'pre-2.bin',
    'wp-3.bin', 'pre-3.bin',
    'wp-tear.bin', 'tear_mask.bin', 'tear_part.bin'
)
foreach ($f in $required) {
    $p = Join-Path $AssetsDir $f
    if (-not (Test-Path $p)) { throw "缺少素材: $p" }
}

$packArgs = @(
    'fdb',
    '--out', $Out,
    '--file', "wp1=$(Join-Path $AssetsDir 'wp-1.bin'),pre=$(Join-Path $AssetsDir 'pre-1.bin')",
    '--file', "wp2=$(Join-Path $AssetsDir 'wp-2.bin'),pre=$(Join-Path $AssetsDir 'pre-2.bin')",
    '--file', "wp3=$(Join-Path $AssetsDir 'wp-3.bin'),pre=$(Join-Path $AssetsDir 'pre-3.bin')",
    '--file', "tb1=$(Join-Path $AssetsDir 'wp-tear.bin'),pre=$(Join-Path $AssetsDir 'pre-1.bin'),mask=$(Join-Path $AssetsDir 'tear_mask.bin'),part=$(Join-Path $AssetsDir 'tear_part.bin')"
)

$videoIncluded = $false
if ($withVideo) {
    # Video is optional: fdb_resource_bin does not include the 4MB wl-video.avi by default.
    $video = Join-Path $AssetsDir 'wl-video.avi'
    if (Test-Path $video) {
        $packArgs += @('--file', "video1=$video")
        $videoIncluded = $true
    }
    else {
        Write-Host "[fdb-build] 提示: $AssetsDir 下没有 wl-video.avi，跳过视频" -ForegroundColor Yellow
    }
}

Write-Host "[fdb-build] assets = $AssetsDir" -ForegroundColor Cyan
Write-Host "[fdb-build] output = $Out" -ForegroundColor Cyan
Write-Host "[fdb-build] video  = $videoIncluded" -ForegroundColor Cyan

# Go through Invoke-Native instead of calling & directly: FlashDB in pack.py writes hints to stderr,
# and this script's $ErrorActionPreference='Stop' would abort on the spot when its output is redirected (see _common.ps1).
Invoke-Native -Exe $Python -Arguments (@(Join-Path $AppDir 'tools\pack\pack.py') + $packArgs)
if ($LASTEXITCODE -ne 0) { throw "pack.py fdb 失败 (exit $LASTEXITCODE)" }

$size = (Get-Item $Out).Length
Write-Host "[fdb-build] done: $Out ($size B)" -ForegroundColor Green
Write-Host "[fdb-build] flash with: .\tools\scripts\flash_fdb.ps1"
