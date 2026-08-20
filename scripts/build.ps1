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

  -Snippet 传任意额外 snippet（可多个），与 -Bank 叠加。目前本工程自带的是
  wifi_8711（外挂 8711 Wi-Fi 的 SPI 挂载，见 note\wifi-8711-spi-skeleton.md）。
  snippet 组合会参与 -d 目录命名（build_wifi_8711 / build_bank0_wifi_8711 ...），
  这样切换组合不会撞上 Zephyr「snippet 集合变了必须 pristine」的限制。
.EXAMPLE
  .\note\build.ps1
  # 增量编译（改完代码后日常用），bin\app.bin
.EXAMPLE
  .\note\build.ps1 -Pristine
  # 干净重建：换配置、换工具链、或 west update 之后用
.EXAMPLE
  .\note\build.ps1 -Snippet wifi_8711
  # 带外挂 8711 Wi-Fi 的 DT/Kconfig 编译 -> build_wifi_8711\
.EXAMPLE
  .\note\build.ps1 -Bank 0 -Snippet wifi_8711 -Pristine
  # OTA bank0 + Wi-Fi -> build_bank0_wifi_8711\, bin\bank0\app_bank0.bin
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
    [string[]]$Snippet = @()    # 额外 snippet（如 wifi_8711），与 -Bank 叠加
)

# ===== 本机路径（换机器只改这一段）=====
$Python = "D:\App\Python\Python312\python.exe"
$SdkDir = "D:\Project\HoneyHmi\bb2u\zephyr-sdk-0.16.8_windows-x86_64_minimal\zephyr-sdk-0.16.8"
$AppDir = "D:\Project\HoneyHmi\zephyrproject\realtek-app\applications\eBadge_8773g_8711f"
# $Mpcli  = "D:\Project\HoneyHmi\zephyrproject\realtek-app\tools\meta_tools\mpcli.exe"
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

# 额外 snippet 拼成目录后缀：-Snippet wifi_8711 -> "_wifi_8711"。
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
    & $Python @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "west build 失败 (bank='$bankSel', exit $LASTEXITCODE)" }

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
        Push-Location $mpcliDir
        try {
            & $Mpcli -c $Port -p -A $addr -F $bin -b 3000000 -M 5 -r -u -d -T RTL87X3G
            $fcode = $LASTEXITCODE
        }
        finally { Pop-Location }
        if ($fcode -ne 0) { throw "mpcli 烧录失败 (exit $fcode)" }
        Write-Host "[flash] OK" -ForegroundColor Green
    }
}

# ===== 编译（结束后恢复原目录）=====
$orig = Get-Location
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
    Set-Location $orig
}
