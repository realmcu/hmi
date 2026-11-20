<#
.SYNOPSIS
  生成预制壁纸 FDB 镜像（tools\pack\fdb_resource_bin\fdb_prebuilt.bin）
  ——Windows 版 build_fdb.sh。

.DESCRIPTION
  素材默认取 tools\pack\fdb_resource_bin（随工程管理）：
    wp-*.bin / pre-*.bin / wp-tear.bin / tear_mask.bin / tear_part.bin /
    wl-video.avi
  该目录没有 wp-1.bin 时退回 HoneyGUI Designer 的构建树
  app\designer\build\assets\images。

  组是显式写死的（不用 pack.py --assets-dir），因为 tear 组需要 4 个槽，
  而自动收集默认会跳过 wp-tear.bin 和 .avi。

  生成后 pack.py 会读回校验（fdb_host_verify），失败即报错。
  烧录用 tools\scripts\flash_fdb.ps1。

.EXAMPLE
  .\tools\scripts\build_fdb.ps1
.EXAMPLE
  .\tools\scripts\build_fdb.ps1 -AssetsDir D:\assets\images -NoVideo
#>

# #requires 必须排在帮助块之后，见 build.ps1 里的说明。
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
    # 视频是可选的：fdb_resource_bin 默认不带 4MB 的 wl-video.avi。
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

# 走 Invoke-Native 而不是直接 &：pack.py 里的 FlashDB 会往 stderr 打提示，
# 本脚本的 $ErrorActionPreference='Stop' 会让它在输出被重定向时当场中止（见 _common.ps1）。
Invoke-Native -Exe $Python -Arguments (@(Join-Path $AppDir 'tools\pack\pack.py') + $packArgs)
if ($LASTEXITCODE -ne 0) { throw "pack.py fdb 失败 (exit $LASTEXITCODE)" }

$size = (Get-Item $Out).Length
Write-Host "[fdb-build] done: $Out ($size B)" -ForegroundColor Green
Write-Host "[fdb-build] flash with: .\tools\scripts\flash_fdb.ps1"
