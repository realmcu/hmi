# 公共路径与工具解析 —— 由 tools/scripts/*.ps1 dot-source（Windows 专用）。
#
#   . (Join-Path $PSScriptRoot '_common.ps1')
#
# dot-source 后调用方可直接使用 $AppDir（= 仓库根）。
# 各函数的 Override 参数优先于环境变量，环境变量优先于默认值。

$AppDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Get-EbadgePython {
    param([string]$Override)
    if ($Override) { return $Override }
    if ($env:EBADGE_PYTHON) { return $env:EBADGE_PYTHON }
    foreach ($name in @('python', 'python3', 'py')) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    throw '找不到 Python：请把 python 加入 PATH，或设置 EBADGE_PYTHON'
}

function Get-EbadgeMpcli {
    param([string]$Override)
    if ($Override) { return $Override }
    if ($env:EBADGE_MPCLI) { return $env:EBADGE_MPCLI }
    return (Join-Path $AppDir 'scripts\tool\mpcli\mpcli.exe')
}

function Get-EbadgePort {
    param([string]$Override)
    if ($Override) { return $Override }
    if ($env:EBADGE_PORT) { return $env:EBADGE_PORT }
    return 'COM5'
}

function Convert-EbadgeHex {
    param([string]$Value)
    if (-not $Value) { throw '空的十六进制值' }
    return [Convert]::ToInt64(($Value -replace '^0[xX]', ''), 16)
}

function Format-EbadgeHex {
    param([int64]$Value)
    return ('0x{0:X8}' -f $Value)
}

function Get-FlashMapValue {
    param([string]$Key)
    $path = Join-Path $AppDir 'flash_map.ini'
    if (-not (Test-Path $path)) { throw "找不到 flash_map.ini: $path" }
    $line = Select-String -Path $path -Pattern "^$([regex]::Escape($Key))=" -List
    if (-not $line) { throw "flash_map.ini 里没有 $Key" }
    return ($line.Line -split '=', 2)[1].Trim()
}

function Get-BankAppAddr {
    param([int]$Bank)
    return Get-FlashMapValue "BANK${Bank}_APP_ADDR"
}

# 调用外部程序。是否成功只看退出码（调用方自己查 $LASTEXITCODE）。
#
# PS 5.1 的坑：脚本输出一旦被重定向或进管道（`2>&1 | Tee-Object`、`*> log.txt`、
# CI 收日志），子进程写 stderr 的每一行都会被包成 ErrorRecord；而 build_fdb.ps1 /
# flash_*.ps1 都设了 $ErrorActionPreference='Stop'，于是那行提示直接变成终止错误，
# 脚本当场中止。偏偏这里调的东西都爱往 stderr 写 —— pack.py 里的 FlashDB 会打提示、
# mpcli.exe 每次运行都回显命令行和版本号（实测 184 B）。结果就是「直接跑没事、
# 一记日志就炸」，中间只差一个 `>`。
#
# 处理办法：显式 2>&1 把 stderr 收进来（不再依赖外层是否重定向），期间把 EAP 放宽成
# Continue 免得终止，最后把 ErrorRecord 还原成普通文本行输出 —— 既保留信息，又不会
# 变成刺眼的红字。
function Invoke-Native {
    param(
        [Parameter(Mandatory)][string]$Exe,
        [string[]]$Arguments = @()
    )
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @Arguments 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) {
                Write-Host $_.ToString()
            }
            else {
                $_
            }
        }
    }
    finally { $ErrorActionPreference = $prev }
}

function Invoke-Mpcli {
    param(
        [string]$Mpcli,
        [string[]]$Arguments,
        [switch]$DryRun
    )
    if ($DryRun) {
        Write-Host ('[dry-run] "{0}" {1}' -f $Mpcli, ($Arguments -join ' '))
        return
    }
    # mpcli 在自身目录下找 fw\、config\，必须从那里启动。
    Push-Location (Split-Path -Parent $Mpcli)
    try {
        Invoke-Native -Exe $Mpcli -Arguments $Arguments
        if ($LASTEXITCODE -ne 0) { throw "mpcli 失败 (exit $LASTEXITCODE)" }
    }
    finally { Pop-Location }
}
