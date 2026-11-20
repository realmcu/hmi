#requires -Version 5
<#
.SYNOPSIS
  One-shot cold-start build for eBadge (plus optional flashing). Ships all environment variables, so a fresh PowerShell window can run it directly.
.DESCRIPTION
  Toolchain = the Realtek-validated Zephyr SDK 0.16.8 (arm-zephyr-eabi), variant=zephyr.
  (Firmware built with gnuarmemb does not run on the Cortex-M55; this toolchain was verified to work. See build-setup.md §9.)

  -Bank selects the OTA dual-bank build (see sample/ota):
    '' (default) no snippet, output to bin\app.bin (old behaviour, for ordinary pre-OTA development builds).
    '0'/'1'  adds --snippet flash_16M_bank<N> and a separate -d build_bank<N>, output to bin\bank<N>\app_bank<N>.bin.
    'both'   builds bank0 and then bank1.
  bank app addresses: bank0=0x7009E000, bank1=0x702F3000 (see flash_map.h BANK0/1_APP_ADDR).

  -Snippet takes any extra snippets (several allowed) and stacks with -Bank. This project ships one:
    wifi   external 8711 Wi-Fi. This side is the SPI host @25MHz with the Realtek standard AT command set.
           See note\spi-master-at-refactor-plan.md

  The old wifi_8711 (local side as slave with a custom AT command set) and spi_at_master (host bring-up
  harness) have been replaced by wifi: with the slave role retired there is no longer a "two snippets
  fighting over the same pads, must pick one" problem, so -Snippet wifi means the host role. Both old
  directories were removed in refactoring stage 5 (along with app\wifi_8711\); dig through git history for the old reference.

  The snippet combination takes part in -d directory naming (build_wifi / build_bank0_wifi ...),
  so switching combinations does not hit Zephyr's "snippet set changed, must be pristine" restriction.

  Each phase (build / flash, and separately per bank when splitting banks) is timed, and ends with a [time] line
  plus a summary table. Failures print too, because "which minute it blew up in" is as worth seeing as the success duration.
  Note this number is only comparable on the same machine: ccache hit rate, other processes stealing CPU, and the
  full CMake configure on the first pristine can all make the same change differ by several times.
.EXAMPLE
  .\note\build.ps1
  # incremental build (daily use after code changes), bin\app.bin
.EXAMPLE
  .\note\build.ps1 -Pristine
  # clean rebuild: use after changing config, changing toolchain, or running west update
.EXAMPLE
  .\note\build.ps1 -Bank 0 -Snippet wifi -Pristine
  # common command: OTA bank0 + external 8711 (SPI host) -> build_bank0_wifi\
  #
  # -Pristine is mandatory the first time, and thereafter every time a .c file is added:
  # app\spi_at_master\CMakeLists.txt uses file(GLOB), so an incremental build does not see new .c files,
  # and the symptom is undefined symbols at link time rather than a compile error. Refactoring will add the
  # at_parser / at_client / at_wifi / at_socket file groups, so this is easier to trip over than before.
.EXAMPLE
  .\note\build.ps1 -Bank 0 -Pristine
  # OTA: clean rebuild of bank0 -> bin\bank0\app_bank0.bin (+ _MP.bin)
.EXAMPLE
  .\note\build.ps1 -Bank both -Pristine
  # OTA: clean rebuild of bank0 and bank1 in turn
.EXAMPLE
  .\note\build.ps1 -Bank 0 -Flash -Port COM7
  # build bank0 and flash it to 0x7009E000 with mpcli
#>
[CmdletBinding()]
param(
    [switch]$Pristine,          # add -p always for a clean rebuild
    [switch]$Flash,             # flash with mpcli after a successful build
    [string]$Port = "COM7",     # flashing serial port (adjust to suit; README uses com3, tasks.json uses com7)
    [ValidateSet('', '0', '1', 'both')]
    [string]$Bank = '',         # build per OTA bank; empty = default single bank (bin\app.bin)
    [string[]]$Snippet = @()    # extra snippets (e.g. wifi), stacked with -Bank
)

# ===== Local paths (the only section to edit on another machine) =====
$Python = "D:\App\Python\Python312\python.exe"
$SdkDir = "D:\Project\HoneyHmi\bb2u\zephyr-sdk-0.16.8_windows-x86_64_minimal\zephyr-sdk-0.16.8"
$AppDir = "D:\Project\HoneyHmi\zephyrproject\realtek-app\applications\eBadge_8773g_8711f"
$Mpcli  = "D:\Project\HoneyHmi\zephyrproject\realtek-app\tools\meta_tools\mpcli.exe"
$Board  = "rtl87x3g_evb"

# ===== Pre-flight checks =====
if (-not (Test-Path $Python)) { throw "找不到 Python 3.12: $Python" }
if (-not (Test-Path "$SdkDir\arm-zephyr-eabi\bin\arm-zephyr-eabi-gcc.exe")) { throw "找不到 Zephyr SDK 工具链: $SdkDir" }
if (-not (Test-Path $AppDir)) { throw "找不到 eBadge 应用目录: $AppDir" }

# ===== Environment variables (the key trio) =====
$env:PYTHONUTF8 = "1"                               # prevents west GBK crashes on Chinese Windows
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"            # use the Realtek-validated SDK toolchain
$env:ZEPHYR_SDK_INSTALL_DIR = $SdkDir
if (Test-Path Env:GNUARMEMB_TOOLCHAIN_PATH) { Remove-Item Env:GNUARMEMB_TOOLCHAIN_PATH }  # clear out leftover gnuarmemb interference
Write-Host "[env]   PYTHONUTF8=1  VARIANT=zephyr  SDK=$SdkDir" -ForegroundColor Cyan

# ===== Timing =====
# Use Stopwatch instead of subtracting two (Get-Date) values: Stopwatch reads a monotonic counter and is not affected
# by the system clock being stepped by NTP or a DST switch. A pristine takes several minutes, exactly the window in which
# the clock may get calibrated; the Get-Date difference can then jump or even go negative, and this number is meant for comparison.
$script:Timings = [System.Collections.Generic.List[object]]::new()

function Format-Duration([TimeSpan]$ts) {
    # Use the .Minutes/.Seconds components of TimeSpan instead of computing remainders from TotalMinutes:
    # PowerShell's [int] cast rounds rather than truncating, so [int]7.705 == 8,
    # and 7m42s becomes 8 minutes minus 17.7 seconds, printing things like "8m-17.7s" (observed in practice).
    $frac = $ts.Seconds + $ts.Milliseconds / 1000.0
    if ($ts.TotalSeconds -lt 60)  { return ('{0:N1}s' -f $ts.TotalSeconds) }
    if ($ts.TotalMinutes -lt 60)  { return ('{0}m{1:00.0}s' -f $ts.Minutes, $frac) }
    return ('{0}h{1:00}m{2:00}s' -f ($ts.Days * 24 + $ts.Hours), $ts.Minutes, $ts.Seconds)
}

# Time a block and record it whether it succeeds or fails.
# Failures are recorded too: "which minute the build blew up in" is useful information by itself, and if only
# the success path recorded, a bank0 failure in -Bank both would make the whole summary table disappear.
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

# Summary table. The sum of the phases is slightly less than the total; the difference is the overhead of the environment and image-existence checks.
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

# Extra snippets are joined into a directory suffix: -Snippet wifi -> "_wifi".
# Each combination gets its own build directory, avoiding Zephyr's "snippet set changed needs pristine" error.
$snipSuffix = if ($Snippet.Count -gt 0) { "_" + ($Snippet -join "_") } else { "" }
if ($Snippet.Count -gt 0) {
    Write-Host "[snip]  $($Snippet -join ', ')" -ForegroundColor Cyan
}

# ===== One build (+ optional flash). $bankSel: ''=default, '0', '1' =====
function Invoke-AppBuild([string]$bankSel) {
    $buildArgs = @("-m", "west", "build", "-b", $Board)
    if ($bankSel -ne '') {
        # split bank: separate build directory + snippet (decides flash partition/active bank), output to bin\bank<N>\
        $buildArgs += @("-d", "build_bank$bankSel$snipSuffix", "--snippet", "flash_16M_bank$bankSel")
    }
    elseif ($snipSuffix -ne '') {
        # only extra snippets, no bank split: still need another directory, or it conflicts with the snippet set of the default build\
        $buildArgs += @("-d", "build$snipSuffix")
    }
    foreach ($s in $Snippet) { $buildArgs += @("--snippet", $s) }
    if ($Pristine) { $buildArgs += @("-p", "always") }
    Write-Host "[build] (bank='$bankSel') $Python $($buildArgs -join ' ')" -ForegroundColor Cyan
    $label = if ($bankSel -eq '') { 'build' } else { "build bank$bankSel" }
    Measure-Phase $label {
        & $Python @buildArgs
        if ($LASTEXITCODE -ne 0) { throw "west build 失败 (bank='$bankSel', exit $LASTEXITCODE)" }
    }

    if ($bankSel -eq '') {
        $bin  = Join-Path $AppDir "bin\app.bin"
        $addr = "0x7009E000"
    }
    else {
        $bin  = Join-Path $AppDir "bin\bank$bankSel\app_bank$bankSel.bin"
        $addr = if ($bankSel -eq '1') { "0x702F3000" } else { "0x7009E000" }
    }
    if (-not (Test-Path $bin)) { throw "编译声称成功但找不到镜像: $bin" }
    Write-Host "[build] OK -> $bin" -ForegroundColor Green

    # ===== Optional flash =====
    if ($Flash) {
        if (-not (Test-Path $Mpcli)) { throw "找不到 mpcli: $Mpcli" }
        # mpcli looks for the RAM patch image fw\RTL87X3G\RTL87X3G_V0_FW.bin in the "current working directory",
        # so it must run from the directory that contains mpcli (tools\meta_tools, which ships fw\); $bin is an absolute path and unaffected.
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

# ===== Build (restore the original directory afterwards) =====
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
    # The summary goes in finally: when the build fails, "how long until it blew up" is as worth seeing as the
    # success duration, and a throw would skip any statement at the end of the try block.
    Set-Location $orig
    $total.Stop()
    Write-TimingSummary $total.Elapsed
}
