<#
.SYNOPSIS
  eBadge 冷启动一键编译（+ 可选烧录）。自带全部环境变量，开新 PowerShell 窗口可直接跑。
.DESCRIPTION
  工具链 = Realtek 验证的 Zephyr SDK 0.16.8 (arm-zephyr-eabi)，variant=zephyr。
  （gnuarmemb 编出的固件在 Cortex-M55 上跑不起来，已换此工具链验证可正常运行。详见 build-setup.md §9。）

  -Bank 用于 OTA 双 bank 构建（参考 sample/ota）：
    ''(默认) 不加 snippet，编到 bin\app.bin（老行为，OTA 前的普通开发编译用）。
    '0'/'1'  加 --snippet flash_32M_bank<N>，独立 -d build_bank<N>，编到 bin\bank<N>\app_bank<N>.bin。
    'both'   依次编 bank0、bank1。
  bank app 地址：运行时读 flash_map.ini 的 BANK<N>_APP_ADDR（当前 bank0=0x7009E000,
  bank1=0x704F3000），不在此脚本里写死。

  -Snippet 传任意额外 snippet（可多个），与 -Bank 叠加。本工程自带：
    wifi   外挂 8711 Wi-Fi。本机做 SPI 主机 @25MHz + Realtek 标准 AT 命令集。
           见 note\spi-master-at-refactor-plan.md

  flash_32M_bank0/1（32M flash 分区表）放在本工程的 snippets\ 下，随本仓库管理，
  不走 realtek-app\snippets。Zephyr 对同名 snippet 是**各追加一次 overlay**而不是
  后者覆盖前者，所以两处各有一份会让分区表叠加两次 —— 编译后会校验解析结果。

  旧的 wifi_8711（本机做从机 + 自研 AT 命令集）与 spi_at_master（主机 bring-up
  harness）已被 wifi 取代：从机角色退役后不再有「两个 snippet 抢同一组 pad、
  必须二选一」的问题，-Snippet wifi 就等于主机角色。两个旧目录已在重构阶段 5
  删除（连同 app\wifi_8711\），要回退参照去翻 git 历史。

  snippet 组合会参与 -d 目录命名（build_wifi / build_bank0_wifi ...），
  这样切换组合不会撞上 Zephyr「snippet 集合变了必须 pristine」的限制。

  每个阶段（编译 / 烧录，分 bank 时各 bank 分开）单独计时，结束打一张 [time] 行 +
  汇总表。失败也会打，因为「跑到第几分钟才炸」和成功耗时一样值得看。
  注意这个数在同一台机器上才可比：ccache 命中率、其它进程抢 CPU、以及第一次
  pristine 要跑完整 CMake 配置，都能让同样的改动差出几倍。
.EXAMPLE
  .\tools\scripts\build.ps1
  # 增量编译（改完代码后日常用），bin\app.bin
.EXAMPLE
  .\tools\scripts\build.ps1 -Pristine
  # 干净重建：换配置、换工具链、或 west update 之后用
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank 0 -Snippet wifi -Pristine
  # 常用命令：OTA bank0 + 外挂 8711（SPI 主机）-> build_bank0_wifi\
  #
  # 首次必须 -Pristine，之后每次「新增 .c 文件」也必须：
  # app\spi_at_master\CMakeLists.txt 用 file(GLOB)，增量构建看不见新增的 .c，
  # 症状是链接期报未定义符号而不是编译错误。重构会陆续加
  # at_parser / at_client / at_wifi / at_socket 四组文件，这条比以前更容易踩。
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank 0 -Pristine
  # OTA：干净重建 bank0 -> bin\bank0\app_bank0.bin (+ _MP.bin)
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank both -Pristine
  # OTA：依次干净重建 bank0 和 bank1
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank 0 -Pack
  # 编译 bank0 并合并 romfs -> bin\bank0\app_bank0_packed.bin
  # 同时导出可单独烧录的 bin\bank0\app_romfs_bank0_reloc.bin
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank both -Pack -NoMp
  # 两个 bank 都打包，不追加 MP 头
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank 0 -Flash -Port COM7
  # 编 bank0 并用 mpcli 烧到 0x7009E000
.EXAMPLE
  .\tools\scripts\build.ps1 -Bank 0 -Pack -Flash -Port COM7
  # 打包并烧录合并后的完整镜像
#>

# #requires 必须排在帮助块之后：它是一条语句而不是注释，放在文件第一行会让
# comment-based help 不再算「位于文件开头」，Get-Help 就只能拿到自动生成的语法行。
#requires -Version 5

[CmdletBinding()]
param(
    [switch]$Pristine,          # 加 -p always 干净重建
    [switch]$Flash,             # 编译成功后用 mpcli 烧录
    [string]$Port = $(if ($env:EBADGE_PORT) { $env:EBADGE_PORT } else { "COM7" }),  # 烧录串口（可用 EBADGE_PORT 覆盖）
    [ValidateSet('', '0', '1', 'both')]
    [string]$Bank = '',         # OTA 分 bank 编译；空=默认单 bank(bin\app.bin)
    [string[]]$Snippet = @(),   # 额外 snippet（如 wifi），与 -Bank 叠加
    [switch]$Pack,              # 编译后把 romfs 合并进镜像（需要显式 -Bank 0/1/both）
    [switch]$NoMp,              # -Pack 时不追加 MP 头
    [string]$Romfs = $env:EBADGE_ROMFS,          # romfs 输入（默认 designer 构建产物）
    [string]$RomfsOffset = $env:EBADGE_ROMFS_OFFSET  # 固定 romfs 偏移，默认自动收敛
)

# ===== 路径（换机器改这里，或用环境变量覆盖）=====
# $AppDir 由脚本位置推出（tools\scripts\ -> 仓库根），不再写死绝对路径。
$AppDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if (-not $Romfs) {
    $Romfs = Join-Path $AppDir 'app\designer\build\app_romfs_0x70536400.bin'
}

# Zephyr SDK：环境变量优先，否则在常见安装位置里挑第一个装好的。
# 不要只写死一条路径 —— 各机器 SDK 版本/位置不同，写死一条的下场是换台机器就
# 「找不到 Zephyr SDK 工具链」而看不出该改哪里。
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

# ===== 前置检查 =====
if (-not (Test-Path $Python)) { throw "找不到 Python 3.12: $Python`n       用 EBADGE_PYTHON 指定，或把 python 加入 PATH" }
if (-not (Test-ZephyrSdk $SdkDir)) {
    $where = if ($SdkDir) { $SdkDir } else { '(未设置)' }
    throw "找不到 Zephyr SDK 工具链: $where`n       把 ZEPHYR_SDK_INSTALL_DIR 或 EBADGE_SDK_DIR 指向 SDK 根目录（其下应有 arm-zephyr-eabi\bin\arm-zephyr-eabi-gcc.exe）"
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "PATH 里没有 cmake：west build 需要 CMake（还有 ninja）。装好后重开 PowerShell，或用 `$env:PATH 指到它的 bin 目录"
}
if (-not (Test-Path $AppDir)) { throw "找不到 eBadge 应用目录: $AppDir" }

# Realtek 的 cmake 有两处要 bash：version_generate.cmake 跑 version_gen_config.sh、
# post_build.cmake 跑 md5_generate.sh。它按 PATH 找 bash.exe，而 Windows 自带的
# ...\AppData\Local\Microsoft\WindowsApps\bash.exe 是 WSL 启动器 —— 它不认 C:/xxx
# 这种 Windows 路径，只会报 "No such file or directory"（exit 127）。后果：VERSION
# 退化成 0.0.0，最后 md5 步骤让整次编译失败。所以必须是 Git Bash / MSYS2 的 bash。
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

# romfs_layout.h 可能在 src\ 或 src\user\（Designer 新导出布局）。
function Get-RomfsLayoutPath {
    $a = Join-Path $AppDir 'app\designer\src\romfs_layout.h'
    $b = Join-Path $AppDir 'app\designer\src\user\romfs_layout.h'
    if (Test-Path $a) { return $a }
    if (Test-Path $b) { return $b }
    if (Test-Path (Split-Path -Parent $b)) { return $b }
    return $a
}

# 读取 pack.py 生成的 ROMFS_OFFSET；打包收敛判断用。
function Get-RomfsOffset([string]$Path) {
    if (-not (Test-Path $Path)) { return $null }
    $m = [regex]::Match((Get-Content -Raw -Encoding UTF8 -Path $Path), 'ROMFS_OFFSET\s+(0x[0-9A-Fa-f]+)')
    if (-not $m.Success) { return $null }
    return ('0x' + $m.Groups[1].Value.Substring(2).ToUpperInvariant())
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
        $buildArgs += @("-d", "build_bank$bankSel$snipSuffix", "--snippet", "flash_32M_bank$bankSel")
    }
    elseif ($snipSuffix -ne '') {
        # 只有额外 snippet、没分 bank：也得换目录，否则跟默认 build\ 的 snippet 集合冲突
        $buildArgs += @("-d", "build$snipSuffix")
    }
    foreach ($s in $Snippet) { $buildArgs += @("--snippet", $s) }
    if ($Pristine) { $buildArgs += @("-p", "always") }

    # find_program 的结果会被 CMake 缓存下来，改了 PATH 也不会重新探测。所以上面那次
    # PATH 修正只对「新 build 目录」和 -Pristine 生效；老目录里若还缓存着 WindowsApps
    # 的 WSL bash，version_gen_config.sh 仍会失败、VERSION 停在 0.0.0。
    # 这里只读检查并提示 —— 不直接改 CMakeCache.txt（改坏整个配置比 0.0.0 的版本号糟糕得多）。
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

    # 校验分区表真的来自本工程：Zephyr 对同名 snippet 是各追加一次 overlay
    # （不是后者覆盖前者），若 realtek-app\snippets 下也有一份，两份会同时生效。
    if ($bankSel -ne '') {
        $want = Join-Path $AppDir "snippets\flash_32M_bank$bankSel\flash_32M_bank$bankSel.overlay"
        $gen  = Join-Path $AppDir "build_bank$bankSel$snipSuffix\zephyr\snippets_generated.cmake"
        $pat  = "EXTRA_DTC_OVERLAY_FILE.*flash_32M_bank$bankSel\.overlay"
        $found = @(Select-String -Path $gen -Pattern $pat -ErrorAction SilentlyContinue)
        if ($found.Count -ne 1) {
            throw "flash_32M_bank$bankSel 解析到 $($found.Count) 份 overlay（应为 1）；若 realtek-app\snippets 下也有同名目录，请删除它，否则分区表会被叠加两次"
        }
        # snippets.py 在 Windows 上也把路径写成 posix 风格，这里统一成正斜杠再比
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

    # 烧录地址以 flash_map.ini 为准（无 bank 的普通编译按 bank0 处理）
    $bankForAddr = if ($bankSel -eq '') { '0' } else { $bankSel }
    $addrLine = Select-String -Path (Join-Path $AppDir 'flash_map.ini') `
                              -Pattern "^BANK${bankForAddr}_APP_ADDR=" -List
    if (-not $addrLine) { throw "flash_map.ini 里没有 BANK${bankForAddr}_APP_ADDR" }
    $addr = ($addrLine.Line -split '=', 2)[1].Trim()
    Write-Host "[build] OK -> $bin (@ $addr)" -ForegroundColor Green

    # ===== 可选：合并 romfs 进 app 镜像 =====
    #
    # romfs 偏移取决于 app 体积，而偏移又写进 romfs_layout.h 影响下次编译，
    # 所以要迭代到收敛：打包 -> 偏移变了就重编 -> 再打包。通常 2 轮内收敛。
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
            # 收敛条件有两个：偏移没变，且固件 ELF 里真的挂着这个地址。
            # 后者是必需的：romfs_layout.h 换过位置（src\ -> src\user\）后，
            # ninja 里旧的依赖已失效，偏移没变也不会重编 Mi_eBadgeEntry.c，
            # ELF 会一直停在旧挂载地址 —— 镜像对、固件错，UI 全黑。
            $reason = $null
            if ($before -ne $after) {
                $reason = "ROMFS_OFFSET $before -> $after"
            }
            elseif ($after) {
                # 显式指定本次用的 ELF：build_bank<N>_wifi 和 build_bank<N> 会同时存在，
                # 让 pack.py 自己挑的话很容易挑到没在用的那个，于是永远判 stale、
                # 每轮都重编，直到 6 轮用完报错。
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
            # 头文件可能刚换了位置，touch 源文件确保 Mi_eBadgeEntry.c 真的重编。
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
