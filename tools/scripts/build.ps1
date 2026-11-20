<#
.SYNOPSIS
  eBadge one-shot cold build (+ optional flashing). It sets up all environment variables itself, so a fresh PowerShell window can run it directly.
.DESCRIPTION
  Toolchain = the Zephyr SDK 0.16.8 (arm-zephyr-eabi) validated by Realtek, variant=zephyr.
  (Firmware built with gnuarmemb will not run on Cortex-M55; with this toolchain it was verified to run normally. See build-setup.md §9.)

  -Bank is for OTA dual-bank builds (see sample/ota):
    '' (default) adds no snippet, output bin\app.bin (old behavior, for ordinary development builds before OTA).
    '0'/'1'  adds --snippet flash_32M_bank<N>, separate -d build_bank<N>, output bin\bank<N>\app_bank<N>.bin.
    'both'   builds bank0 then bank1.
  bank app address: read at run time from BANK<N>_APP_ADDR in flash_map.ini (currently bank0=0x7009E000,
  bank1=0x704F3000); it is not hardcoded in this script.

  -Snippet passes any additional snippet (repeatable) and stacks with -Bank. This project ships:
    wifi   external 8711 Wi-Fi. This device acts as SPI host @25MHz with the Realtek standard AT command set.
           See note\spi-master-at-refactor-plan.md

  flash_32M_bank0/1 (the 32M flash partition table) lives under this project's snippets\, tracked by this repo,
  and does not use realtek-app\snippets. For snippets with the same name Zephyr **appends each overlay** instead of
  letting the latter override the former, so having one in both places applies the partition table twice — the resolved result is checked after building.

  The old wifi_8711 (device acts as slave + custom AT command set) and spi_at_master (host bring-up
  harness) were replaced by wifi: now that the slave role is retired there is no longer the "two snippets fight over the same pads and
  one must win" problem, and -Snippet wifi simply means the host role. Both old directories were deleted in refactoring phase 5
  (together with app\wifi_8711\); for the old reference, look through git history.

  The snippet combination contributes to the -d directory name (build_wifi / build_bank0_wifi ...),
  so switching combinations does not hit Zephyr's "snippet set changed, pristine required" restriction.

  Each phase (build / flash, and each bank separately when building per bank) is timed on its own, printing a [time] line plus
  a summary table at the end. Failures are timed too, because "which minute it blew up in" is as worth seeing as the success duration.
  Note that these numbers are only comparable on the same machine: ccache hit rate, other processes competing for CPU, and the first
  pristine run doing a full CMake configure can all make the same change differ by several times.
.EXAMPLE
  .\tools\scripts\build.ps1
  # incremental build (for daily use after editing code), bin\app.bin
.EXAMPLE
  .\tools\scripts\build.ps1 -Pristine
  # clean rebuild: use after changing configuration or toolchain, or after west update
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank 0 -Snippet wifi -Pristine
  # common command: OTA bank0 + external 8711 (SPI host) -> build_bank0_wifi\
  #
  # -Pristine is required the first time, and every time a new .c file is added:
  # app\spi_at_master\CMakeLists.txt uses file(GLOB), so incremental builds do not see new .c files
  # and the symptom is an undefined symbol at link time rather than a compile error. The refactoring adds files
  # in four groups (at_parser / at_client / at_wifi / at_socket), so this is easier to hit than before.
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank 0 -Pristine
  # OTA: clean rebuild of bank0 -> bin\bank0\app_bank0.bin (+ _MP.bin)
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank both -Pristine
  # OTA: clean rebuild bank0 and bank1 in order
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank 0 -Pack
  # build bank0 and merge romfs -> bin\bank0\app_bank0_packed.bin
  # also exports bin\bank0\app_romfs_bank0_reloc.bin for standalone flashing
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank both -Pack -NoMp
  # pack both banks, do not append the MP header
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank 0 -Flash -Port COM7
  # build bank0 and flash it to 0x7009E000 with mpcli
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank 0 -Pack -Flash -Port COM7
  # pack and flash the merged full image
#>

# #requires must come after the help block: it is a statement, not a comment, and putting it on the first line means
# comment-based help no longer counts as "at the top of the file", so Get-Help only shows the auto-generated syntax line.
#requires -Version 5

[CmdletBinding()]
param(
    [switch]$Pristine,          # add -p always for a clean rebuild
    [switch]$Flash,             # flash with mpcli after a successful build
    [string]$Port = $(if ($env:EBADGE_PORT) { $env:EBADGE_PORT } else { "COM7" }),  # flash serial port (overridable with EBADGE_PORT)
    [ValidateSet('', '0', '1', 'both')]
    [string]$Bank = '',         # OTA per-bank build; empty = default single bank (bin\app.bin)
    [string[]]$Snippet = @(),   # extra snippets (e.g. wifi), stacked with -Bank
    [switch]$Pack,              # merge romfs into the image after building (needs an explicit -Bank 0/1/both)
    [switch]$NoMp,              # do not append the MP header with -Pack
    [string]$Romfs = $env:EBADGE_ROMFS,          # romfs input (default: the designer build artifact)
    [string]$RomfsOffset = $env:EBADGE_ROMFS_OFFSET  # fixed romfs offset; by default it converges automatically
)

# ===== paths (change here on another machine, or override with environment variables) =====
# $AppDir is derived from the script location (tools\scripts\ -> repo root); no absolute path is hardcoded.
$AppDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if (-not $Romfs) {
    $Romfs = Join-Path $AppDir 'app\designer\build\app_romfs_0x70536400.bin'
}

# Zephyr SDK: environment variables win, otherwise pick the first installed one among the common locations.
# Do not hardcode a single path — each machine has a different SDK version/location, and hardcoding one means
# another machine reports "Zephyr SDK toolchain not found" with no hint about what to change.
function Test-ZephyrSdk([string]$Dir) {
    return ($Dir -and (Test-Path (Join-Path $Dir 'arm-zephyr-eabi\bin\arm-zephyr-eabi-gcc.exe')))
}
$SdkDir = if ($env:EBADGE_SDK_DIR) { $env:EBADGE_SDK_DIR }
          elseif ($env:ZEPHYR_SDK_INSTALL_DIR) { $env:ZEPHYR_SDK_INSTALL_DIR }
          else {
              @('C:\zephyr-sdk-0.16.9',
                'C:\zephyr-sdk-0.16.8',
                'D:\zephyr-sdk-0.16.9',
                'D:\zephyr-sdk-0.16.8',
                'D:\Project\HoneyHmi\bb2u\zephyr-sdk-0.16.8_windows-x86_64_minimal\zephyr-sdk-0.16.8') |
                  Where-Object { Test-ZephyrSdk $_ } | Select-Object -First 1
          }

$Python = if ($env:EBADGE_PYTHON) { $env:EBADGE_PYTHON }
          elseif (Get-Command python -ErrorAction SilentlyContinue) { (Get-Command python).Source }
          else { "D:\App\Python\Python312\python.exe" }
$Mpcli  = if ($env:EBADGE_MPCLI)   { $env:EBADGE_MPCLI }   else { Join-Path $AppDir 'scripts\tool\mpcli\mpcli.exe' }
$Board  = if ($env:EBADGE_BOARD)   { $env:EBADGE_BOARD }   else { "rtl87x3g_evb" }

# ===== preflight checks =====
if (-not (Test-Path $Python)) { throw "找不到 Python 3.12: $Python`n       用 EBADGE_PYTHON 指定，或把 python 加入 PATH" }
if (-not (Test-ZephyrSdk $SdkDir)) {
    $where = if ($SdkDir) { $SdkDir } else { '(未设置)' }
    throw "找不到 Zephyr SDK 工具链: $where`n       把 ZEPHYR_SDK_INSTALL_DIR 或 EBADGE_SDK_DIR 指向 SDK 根目录（其下应有 arm-zephyr-eabi\bin\arm-zephyr-eabi-gcc.exe）"
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "PATH 里没有 cmake：west build 需要 CMake（还有 ninja）。装好后重开 PowerShell，或用 `$env:PATH 指到它的 bin 目录"
}
if (-not (Test-Path $AppDir)) { throw "找不到 eBadge 应用目录: $AppDir" }

# Realtek's cmake needs bash in two places: version_generate.cmake runs version_gen_config.sh
# and post_build.cmake runs md5_generate.sh. It looks for bash.exe on PATH, but the Windows-shipped
# ...\AppData\Local\Microsoft\WindowsApps\bash.exe is the WSL launcher — it does not understand Windows
# paths like C:/xxx and only reports "No such file or directory" (exit 127). The consequence: VERSION
# degrades to 0.0.0 and the final md5 step fails the whole build. So it must be Git Bash / MSYS2 bash.
$bashCmd = Get-Command bash -ErrorAction SilentlyContinue
if (-not $bashCmd -or $bashCmd.Source -like '*\WindowsApps\bash.exe') {
    $gitBash = @("$env:ProgramFiles\Git\bin\bash.exe",
                 "${env:ProgramFiles(x86)}\Git\bin\bash.exe",
                 'C:\Program Files\Git\bin\bash.exe',
                 'C:\wokspace\software\Git\bin\bash.exe') |
                 Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $gitBash) {
        throw ("PATH 里没有可用的 bash.exe（Git Bash / MSYS2 都要）`n" +
               "       Realtek 的 cmake 要用它生成版本号和 md5；WindowsApps 下的那个是 WSL 启动器，不能用`n" +
               "       装 Git for Windows 即可，或把其 bin 目录加到 PATH")
    }
    $env:PATH = (Split-Path -Parent $gitBash) + ';' + $env:PATH
    Write-Host "[env]   bash=$gitBash（已放到 PATH 最前，盖住 WindowsApps 的 WSL bash）" -ForegroundColor Yellow
}
foreach ($b in @('0', '1')) {
    if (-not (Test-Path (Join-Path $AppDir "snippets\flash_32M_bank$b\snippet.yml"))) {
        throw "找不到本工程的分区表 snippet: snippets\flash_32M_bank$b\snippet.yml"
    }
}

# ===== environment variables (the key trio) =====
$env:PYTHONUTF8 = "1"                               # prevent west GBK crashes on Chinese Windows
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"            # use the SDK toolchain validated by Realtek
$env:ZEPHYR_SDK_INSTALL_DIR = $SdkDir
if (Test-Path Env:GNUARMEMB_TOOLCHAIN_PATH) { Remove-Item Env:GNUARMEMB_TOOLCHAIN_PATH }  # clear leftover gnuarmemb interference
Write-Host "[env]   PYTHONUTF8=1  VARIANT=zephyr  SDK=$SdkDir" -ForegroundColor Cyan

# ===== timing =====
# Use Stopwatch instead of subtracting two (Get-Date) values: Stopwatch reads a monotonic counter and is unaffected
# by NTP clock adjustments or daylight-saving switches. A pristine build takes several minutes — exactly the window
# in which the clock may be recalibrated, so a Get-Date difference can jump or go negative, and these numbers are compared across runs.
$script:Timings = [System.Collections.Generic.List[object]]::new()

function Format-Duration([TimeSpan]$ts) {
    # Use TimeSpan's own .Minutes/.Seconds components instead of computing the remainder from TotalMinutes:
    # PowerShell's [int] cast rounds rather than truncates, [int]7.705 == 8,
    # so 7m42s becomes 8 minutes minus 17.7 seconds and prints things like "8m-17.7s" (actually hit in practice).
    $frac = $ts.Seconds + $ts.Milliseconds / 1000.0
    if ($ts.TotalSeconds -lt 60)  { return ('{0:N1}s' -f $ts.TotalSeconds) }
    if ($ts.TotalMinutes -lt 60)  { return ('{0}m{1:00.0}s' -f $ts.Minutes, $frac) }
    return ('{0}h{1:00}m{2:00}s' -f ($ts.Days * 24 + $ts.Hours), $ts.Minutes, $ts.Seconds)
}

# Time a block and record it whether it succeeds or fails.
# Failures are recorded too: "which minute the build blew up in" is useful information by itself, and if only
# the success path recorded, a bank0 failure under -Bank both would make the whole summary table disappear.
function Measure-Phase([string]$name, [scriptblock]$body) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $ok = $false
    try {
        & $body
        $ok = $true
    }
    finally {
        $sw.Stop()
        $script:Timings.Add([pscustomobject]@{
            Name = $name; Elapsed = $sw.Elapsed; Ok = $ok
        })
        Write-Host ("[time]  {0} {1} {2}" -f $name, $(if ($ok) { 'OK' } else { '失败' }),
                                             (Format-Duration $sw.Elapsed)) -ForegroundColor DarkGray
    }
}

# Summary table. The sum of the phases is slightly less than the total; the difference is the overhead of the environment and image-presence checks.
function Write-TimingSummary([TimeSpan]$total) {
    if ($script:Timings.Count -eq 0) { return }
    $mode = if ($Pristine) { 'pristine 干净重建' } else { '增量' }
    Write-Host ""
    Write-Host "===== 耗时（$mode）=====" -ForegroundColor Cyan
    foreach ($t in $script:Timings) {
        $color = if ($t.Ok) { 'Green' } else { 'Red' }
        Write-Host ("  {0,-24} {1,10}{2}" -f $t.Name, (Format-Duration $t.Elapsed),
                                            $(if ($t.Ok) { '' } else { '   <-- 失败' })) -ForegroundColor $color
    }
    if ($script:Timings.Count -gt 1) {
        Write-Host ("  {0,-24} {1,10}" -f '总计', (Format-Duration $total)) -ForegroundColor Cyan
    }
}

# romfs_layout.h may live in src\ or src\user\ (the new Designer export layout).
function Get-RomfsLayoutPath {
    $a = Join-Path $AppDir 'app\designer\src\romfs_layout.h'
    $b = Join-Path $AppDir 'app\designer\src\user\romfs_layout.h'
    if (Test-Path $a) { return $a }
    if (Test-Path $b) { return $b }
    if (Test-Path (Split-Path -Parent $b)) { return $b }
    return $a
}

# Read the ROMFS_OFFSET produced by pack.py; used for the packing convergence check.
function Get-RomfsOffset([string]$Path) {
    if (-not (Test-Path $Path)) { return $null }
    $m = [regex]::Match((Get-Content -Raw -Encoding UTF8 -Path $Path), 'ROMFS_OFFSET\s+(0x[0-9A-Fa-f]+)')
    if (-not $m.Success) { return $null }
    return ('0x' + $m.Groups[1].Value.Substring(2).ToUpperInvariant())
}

# Extra snippets are joined into a directory suffix: -Snippet wifi -> "_wifi".
# Each combination gets its own build directory, avoiding Zephyr's "snippet set changed, pristine required" error.
$snipSuffix = if ($Snippet.Count -gt 0) { "_" + ($Snippet -join "_") } else { "" }
if ($Snippet.Count -gt 0) {
    Write-Host "[snip]  $($Snippet -join ', ')" -ForegroundColor Cyan
}

# ===== one build (+ optional flashing). $bankSel: ''=default, '0', '1' =====
function Invoke-AppBuild([string]$bankSel) {
    $buildArgs = @("-m", "west", "build", "-b", $Board)
    if ($bankSel -ne '') {
        # per bank: separate build directory + snippet (decides flash partitions/active bank), output to bin\bank<N>\
        $buildArgs += @("-d", "build_bank$bankSel$snipSuffix", "--snippet", "flash_32M_bank$bankSel")
    }
    elseif ($snipSuffix -ne '') {
        # only extra snippets, no bank split: still needs another directory, or it conflicts with the snippet set of the default build\
        $buildArgs += @("-d", "build$snipSuffix")
    }
    foreach ($s in $Snippet) { $buildArgs += @("--snippet", $s) }
    if ($Pristine) { $buildArgs += @("-p", "always") }

    # find_program results are cached by CMake and not re-probed when PATH changes. So the PATH fix
    # above only takes effect for new build directories and for -Pristine; if an old directory still caches
    # the WindowsApps WSL bash, version_gen_config.sh still fails and VERSION stays at 0.0.0.
    # This only checks read-only and warns — it does not rewrite CMakeCache.txt (corrupting the whole configuration is far worse than a 0.0.0 version number).
    $buildDir = if ($bankSel -ne '') { "build_bank$bankSel$snipSuffix" }
                elseif ($snipSuffix -ne '') { "build$snipSuffix" }
                else { 'build' }
    $cacheFile = Join-Path $AppDir "$buildDir\CMakeCache.txt"
    if ((Test-Path $cacheFile) -and (Select-String -Path $cacheFile -Pattern '^Bash_EXECUTABLE:.*WindowsApps' -Quiet)) {
        Write-Host "[env]   警告: $buildDir 缓存的是 WSL bash，VERSION 会是 0.0.0（加 -Pristine 可修正）" -ForegroundColor Yellow
    }

    Write-Host "[build] (bank='$bankSel') $Python $($buildArgs -join ' ')" -ForegroundColor Cyan
    $label = if ($bankSel -eq '') { 'build' } else { "build bank$bankSel" }
    Measure-Phase $label {
        & $Python @buildArgs
        if ($LASTEXITCODE -ne 0) { throw "west build 失败 (bank='$bankSel', exit $LASTEXITCODE)" }
    }

    # Check that the partition table really comes from this project: for same-named snippets Zephyr appends
    # each overlay separately (the latter does not override the former), so if realtek-app\snippets has one too, both take effect.
    if ($bankSel -ne '') {
        $want = Join-Path $AppDir "snippets\flash_32M_bank$bankSel\flash_32M_bank$bankSel.overlay"
        $gen  = Join-Path $AppDir "build_bank$bankSel$snipSuffix\zephyr\snippets_generated.cmake"
        $pat  = "EXTRA_DTC_OVERLAY_FILE.*flash_32M_bank$bankSel\.overlay"
        $found = @(Select-String -Path $gen -Pattern $pat -ErrorAction SilentlyContinue)
        if ($found.Count -ne 1) {
            throw "flash_32M_bank$bankSel 解析到 $($found.Count) 份 overlay（应为 1）；若 realtek-app\snippets 下也有同名目录，请删除它，否则分区表会被叠加两次"
        }
        # snippets.py writes paths in posix style even on Windows, so normalize to forward slashes before comparing
        $wantPosix = $want.Replace('\', '/')
        if ($found[0].Line -notmatch [regex]::Escape($wantPosix)) {
            throw "分区表不是取自本工程: $want"
        }
        Write-Host "[snip]  分区表 OK <- $want" -ForegroundColor Green
    }

    if ($bankSel -eq '') {
        $bin = Join-Path $AppDir "bin\app.bin"
    }
    else {
        $bin = Join-Path $AppDir "bin\bank$bankSel\app_bank$bankSel.bin"
    }
    if (-not (Test-Path $bin)) { throw "编译声称成功但找不到镜像: $bin" }

    # The flash address comes from flash_map.ini (a plain build with no bank is treated as bank0)
    $bankForAddr = if ($bankSel -eq '') { '0' } else { $bankSel }
    $addrLine = Select-String -Path (Join-Path $AppDir 'flash_map.ini') `
                              -Pattern "^BANK${bankForAddr}_APP_ADDR=" -List
    if (-not $addrLine) { throw "flash_map.ini 里没有 BANK${bankForAddr}_APP_ADDR" }
    $addr = ($addrLine.Line -split '=', 2)[1].Trim()
    Write-Host "[build] OK -> $bin (@ $addr)" -ForegroundColor Green

    # ===== optional: merge romfs into the app image =====
    #
    # The romfs offset depends on the size of app, and the offset is written into romfs_layout.h affecting the next build,
    # so iterate to convergence: pack -> rebuild if the offset changed -> pack again. It usually converges within 2 rounds.
    if ($Pack) {
        if ($bankSel -eq '') { throw '-Pack 需要显式 -Bank 0 / 1 / both' }
        if (-not (Test-Path $Romfs)) { throw "找不到 romfs: $Romfs" }

        $layoutH = Get-RomfsLayoutPath
        $packPy = Join-Path $AppDir 'tools\pack\pack.py'
        $merged = Join-Path $AppDir "bin\bank$bankSel\app_bank${bankSel}_packed.bin"
        $romfsOnly = Join-Path $AppDir "bin\bank$bankSel\app_romfs_bank${bankSel}_reloc.bin"
        $round = 0
        while ($true) {
            $round++
            $before = Get-RomfsOffset $layoutH
            $packArgs = @('app', '--bank', "bank$bankSel",
                          '--app', $bin, '--romfs', $Romfs,
                          '--out', $merged, '--sign', '--romfs-out', $romfsOnly)
            if (-not $NoMp) { $packArgs += '--mp' }
            if ($RomfsOffset) { $packArgs += @('--romfs-offset', $RomfsOffset) }

            Measure-Phase "pack bank$bankSel (round $round)" {
                & $Python $packPy @packArgs
                if ($LASTEXITCODE -ne 0) { throw "pack.py 失败 (exit $LASTEXITCODE)" }
            }

            $after = Get-RomfsOffset $layoutH
            # There are two convergence conditions: the offset did not change, and the firmware ELF really mounts this address.
            # The latter is essential: after romfs_layout.h moved (src\ -> src\user\),
            # the old ninja dependency is stale, so Mi_eBadgeEntry.c is not rebuilt even when the offset is unchanged,
            # and the ELF stays stuck at the old mount address — right image, wrong firmware, all-black UI.
            $reason = $null
            if ($before -ne $after) {
                $reason = "ROMFS_OFFSET $before -> $after"
            }
            elseif ($after) {
                # Specify the ELF used this round explicitly: build_bank<N>_wifi and build_bank<N> can both exist,
                # and if pack.py picks by itself it easily picks the unused one, so it always reports stale
                # and rebuilds every round until the 6 rounds are used up and it errors out.
                $elf = Join-Path $AppDir "$buildDir\zephyr\zephyr.elf"
                & $Python $packPy fwcheck --bank "bank$bankSel" --offset $after --elf $elf *> $null
                if ($LASTEXITCODE -eq 2) {
                    $reason = "固件 ELF 里的 romfs 挂载地址不是 $after"
                }
            }
            if (-not $reason) {
                Write-Host "[pack] 收敛于 ROMFS_OFFSET=$after（第 $round 轮）" -ForegroundColor Green
                break
            }
            if ($round -ge 6) {
                throw "[pack] 6 轮仍未收敛（$reason），放弃"
            }
            Write-Host "[pack] $reason，重编 app 后重试" -ForegroundColor Yellow
            # The header may just have moved, so touch the source to make sure Mi_eBadgeEntry.c is really rebuilt.
            (Get-Item (Join-Path $AppDir 'app\designer\src\Mi_eBadgeEntry.c')).LastWriteTime = Get-Date

            $rebuildArgs = @('-m', 'west', 'build', '-b', $Board,
                             '-d', "build_bank$bankSel$snipSuffix",
                             '--snippet', "flash_32M_bank$bankSel")
            foreach ($s in $Snippet) { $rebuildArgs += @('--snippet', $s) }
            Measure-Phase "rebuild bank$bankSel ($round)" {
                & $Python @rebuildArgs
                if ($LASTEXITCODE -ne 0) { throw "west build 失败 (exit $LASTEXITCODE)" }
            }
        }

        $bin = $merged
        Write-Host "[pack] OK -> $bin" -ForegroundColor Green
        Write-Host "[pack] 只更新壁纸: .\tools\scripts\flash_romfs.ps1 -Bank $bankSel"
    }

    # ===== optional flashing =====
    if ($Flash) {
        if (-not (Test-Path $Mpcli)) { throw "找不到 mpcli: $Mpcli" }
        # mpcli looks for the RAM patch image fw\RTL87X3G\RTL87X3G_V0_FW.bin under the current working directory,
        # so it must run from the mpcli directory (tools\meta_tools, which ships fw\); $bin is an absolute path and unaffected.
        $mpcliDir = Split-Path $Mpcli
        Write-Host "[flash] $Port  <-  $bin  ($addr / RTL87X3G)" -ForegroundColor Cyan
        $label = if ($bankSel -eq '') { 'flash' } else { "flash bank$bankSel" }
        Measure-Phase $label {
            Push-Location $mpcliDir
            try {
                & $Mpcli -c $Port -p -A $addr -F $bin -b 3000000 -M 5 -r -u -d -T RTL87X3G
                $fcode = $LASTEXITCODE
            }
            finally { Pop-Location }
            if ($fcode -ne 0) { throw "mpcli 烧录失败 (exit $fcode)" }
        }
        Write-Host "[flash] OK" -ForegroundColor Green
    }
}

# ===== build (restore the original directory afterwards) =====
$orig  = Get-Location
$total = [System.Diagnostics.Stopwatch]::StartNew()
try {
    Set-Location $AppDir
    switch ($Bank) {
        'both'  { Invoke-AppBuild '0'; Invoke-AppBuild '1' }
        '0'     { Invoke-AppBuild '0' }
        '1'     { Invoke-AppBuild '1' }
        default { Invoke-AppBuild '' }
    }
}
finally {
    # The summary lives in finally: on failure "how long until it blew up" is as worth seeing as a success duration,
    # and a throw skips any statement at the end of try.
    Set-Location $orig
    $total.Stop()
    Write-TimingSummary $total.Elapsed
}
