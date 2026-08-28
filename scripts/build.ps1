#requires -Version 5
<#
.SYNOPSIS
  eBadge 冷启动一键编译（+ 可选烧录）。自带全部环境变量，开新 PowerShell 窗口可直接跑。
.DESCRIPTION
  工具链 = Realtek 验证的 Zephyr SDK 0.16.8 (arm-zephyr-eabi)，variant=zephyr。
  （gnuarmemb 编出的固件在 Cortex-M55 上跑不起来，已换此工具链验证可正常运行。详见 build-setup.md §9。）

  -Bank 用于 OTA 双 bank 构建（参考 sample/ota）：
    ''(默认) 不加 snippet，编到 bin\app.bin（老行为，OTA 前的普通开发编译用）。
    '0'/'1'  加 --snippet flash_16M_bank<N>，独立 -d build_bank<N>，编到 bin\bank<N>\app_bank<N>.bin。
    'both'   依次编 bank0、bank1。
  bank app 地址： bank0=0x7009E000, bank1=0x702F3000（见 flash_map.h BANK0/1_APP_ADDR）。

  -Snippet 传任意额外 snippet（可多个），与 -Bank 叠加。本工程自带一个：
    wifi   外挂 8711 Wi-Fi。本机做 SPI 主机 @25MHz + Realtek 标准 AT 命令集。
           见 note\spi-master-at-refactor-plan.md

  旧的 wifi_8711（本机做从机 + 自研 AT 命令集）与 spi_at_master（主机 bring-up
  harness）已被 wifi 取代：从机角色退役后不再有「两个 snippet 抢同一组 pad、
  必须二选一」的问题，-Snippet wifi 就等于主机角色。两个旧目录在重构阶段 5 删除，
  在那之前仍可编译，作为回退参照。

  snippet 组合会参与 -d 目录命名（build_wifi / build_bank0_wifi ...），
  这样切换组合不会撞上 Zephyr「snippet 集合变了必须 pristine」的限制。

  每个阶段（编译 / 烧录，分 bank 时各 bank 分开）单独计时，结束打一张 [time] 行 +
  汇总表。失败也会打，因为「跑到第几分钟才炸」和成功耗时一样值得看。
  注意这个数在同一台机器上才可比：ccache 命中率、其它进程抢 CPU、以及第一次
  pristine 要跑完整 CMake 配置，都能让同样的改动差出几倍。
.EXAMPLE
  .\note\build.ps1
  # 增量编译（改完代码后日常用），bin\app.bin
.EXAMPLE
  .\note\build.ps1 -Pristine
  # 干净重建：换配置、换工具链、或 west update 之后用
.EXAMPLE
  .\note\build.ps1 -Bank 0 -Snippet wifi -Pristine
  # 常用命令：OTA bank0 + 外挂 8711（SPI 主机）-> build_bank0_wifi\
  #
  # 首次必须 -Pristine，之后每次「新增 .c 文件」也必须：
  # app\spi_at_master\CMakeLists.txt 用 file(GLOB)，增量构建看不见新增的 .c，
  # 症状是链接期报未定义符号而不是编译错误。重构会陆续加
  # at_parser / at_client / at_wifi / at_socket 四组文件，这条比以前更容易踩。
.EXAMPLE
  .\note\build.ps1 -Bank 0 -Pristine
  # OTA：干净重建 bank0 -> bin\bank0\app_bank0.bin (+ _MP.bin)
.EXAMPLE
  .\note\build.ps1 -Bank both -Pristine
  # OTA：依次干净重建 bank0 和 bank1
.EXAMPLE
  .\note\build.ps1 -Bank 0 -Flash -Port COM7
  # 编 bank0 并用 mpcli 烧到 0x7009E000
#>
[CmdletBinding()]
param(
    [switch]$Pristine,          # 加 -p always 干净重建
    [switch]$Flash,             # 编译成功后用 mpcli 烧录
    [string]$Port = "COM7",     # 烧录串口（按实际改；README 用 com3，tasks.json 用 com7）
    [ValidateSet('', '0', '1', 'both')]
    [string]$Bank = '',         # OTA 分 bank 编译；空=默认单 bank(bin\app.bin)
    [string[]]$Snippet = @()    # 额外 snippet（如 wifi），与 -Bank 叠加
)

# ===== 本机路径（换机器只改这一段）=====
$Python = "D:\App\Python\Python312\python.exe"
$SdkDir = "D:\Project\HoneyHmi\bb2u\zephyr-sdk-0.16.8_windows-x86_64_minimal\zephyr-sdk-0.16.8"
$AppDir = "D:\Project\HoneyHmi\zephyrproject\realtek-app\applications\eBadge_8773g_8711f"
$Mpcli  = "D:\Project\HoneyHmi\zephyrproject\realtek-app\tools\meta_tools\mpcli.exe"
$Board  = "rtl87x3g_evb"

# ===== 前置检查 =====
if (-not (Test-Path $Python)) { throw "找不到 Python 3.12: $Python" }
if (-not (Test-Path "$SdkDir\arm-zephyr-eabi\bin\arm-zephyr-eabi-gcc.exe")) { throw "找不到 Zephyr SDK 工具链: $SdkDir" }
if (-not (Test-Path $AppDir)) { throw "找不到 eBadge 应用目录: $AppDir" }

# ===== 环境变量（关键三件套）=====
$env:PYTHONUTF8 = "1"                               # 中文 Windows 防 west GBK 崩溃
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"            # 用 Realtek 验证的 SDK 工具链
$env:ZEPHYR_SDK_INSTALL_DIR = $SdkDir
if (Test-Path Env:GNUARMEMB_TOOLCHAIN_PATH) { Remove-Item Env:GNUARMEMB_TOOLCHAIN_PATH }  # 清掉旧 gnuarmemb 干扰
Write-Host "[env]   PYTHONUTF8=1  VARIANT=zephyr  SDK=$SdkDir" -ForegroundColor Cyan

# ===== 耗时统计 =====
# 用 Stopwatch 而不是两次 (Get-Date) 相减：Stopwatch 读的是单调计数器，不受系统时间被
# NTP 拨动或夏令时切换影响。一次 pristine 要好几分钟，正好是「期间时钟被校准一下」的
# 窗口，那时候 Get-Date 之差能给出跳变甚至负数，而这个数是要拿来横向比对的。
$script:Timings = [System.Collections.Generic.List[object]]::new()

function Format-Duration([TimeSpan]$ts) {
    # 用 TimeSpan 自带的 .Minutes/.Seconds 分量，不要拿 TotalMinutes 自己算余数：
    # PowerShell 的 [int] 转换是「四舍五入」不是「截断」，[int]7.705 == 8，
    # 于是 7m42s 会被算成 8 分 -17.7 秒，打出 "8m-17.7s" 这种东西（已实测踩过）。
    $frac = $ts.Seconds + $ts.Milliseconds / 1000.0
    if ($ts.TotalSeconds -lt 60)  { return ('{0:N1}s' -f $ts.TotalSeconds) }
    if ($ts.TotalMinutes -lt 60)  { return ('{0}m{1:00.0}s' -f $ts.Minutes, $frac) }
    return ('{0}h{1:00}m{2:00}s' -f ($ts.Days * 24 + $ts.Hours), $ts.Minutes, $ts.Seconds)
}

# 计时跑一段，无论成功失败都记账。
# 失败也记：「编译到第几分钟才炸」本身就是有用的信息，而且如果只在成功路径上记账，
# -Bank both 里 bank0 失败会让汇总表整个消失。
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

# 汇总表。各阶段之和会略小于总耗时，差值是环境检查与镜像存在性检查那点开销。
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

# 额外 snippet 拼成目录后缀：-Snippet wifi -> "_wifi"。
# 每种组合独占一个 build 目录，避免 Zephyr 的 "snippet 集合变化需 pristine" 报错。
$snipSuffix = if ($Snippet.Count -gt 0) { "_" + ($Snippet -join "_") } else { "" }
if ($Snippet.Count -gt 0) {
    Write-Host "[snip]  $($Snippet -join ', ')" -ForegroundColor Cyan
}

# ===== 单次编译(+可选烧录)。$bankSel: ''=默认, '0', '1' =====
function Invoke-AppBuild([string]$bankSel) {
    $buildArgs = @("-m", "west", "build", "-b", $Board)
    if ($bankSel -ne '') {
        # 分 bank：独立 build 目录 + snippet（决定 flash 分区/active bank），输出到 bin\bank<N>\
        $buildArgs += @("-d", "build_bank$bankSel$snipSuffix", "--snippet", "flash_16M_bank$bankSel")
    }
    elseif ($snipSuffix -ne '') {
        # 只有额外 snippet、没分 bank：也得换目录，否则跟默认 build\ 的 snippet 集合冲突
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

    # ===== 可选烧录 =====
    if ($Flash) {
        if (-not (Test-Path $Mpcli)) { throw "找不到 mpcli: $Mpcli" }
        # mpcli 在「当前工作目录」下找 RAM patch 镜像 fw\RTL87X3G\RTL87X3G_V0_FW.bin，
        # 必须从 mpcli 所在目录（tools\meta_tools，其下自带 fw\）跑；$bin 是绝对路径不受影响。
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

# ===== 编译（结束后恢复原目录）=====
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
    # 汇总放在 finally 里：编译失败时「跑了多久才炸」和成功时的耗时一样值得看，
    # 而 throw 会跳过 try 尾部的任何语句。
    Set-Location $orig
    $total.Stop()
    Write-TimingSummary $total.Elapsed
}
