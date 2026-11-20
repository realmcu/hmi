# Common path and tool resolution — dot-sourced by tools/scripts/*.ps1 (Windows only).
#
#   . (Join-Path $PSScriptRoot '_common.ps1')
#
# After dot-sourcing, callers can use $AppDir directly (= repo root).
# Each function's Override parameter wins over env vars; env vars win over defaults.

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

# Invoke an external program. Success is judged only by the exit code (callers check $LASTEXITCODE themselves).
#
# PS 5.1 pitfall: once script output is redirected or piped (`2>&1 | Tee-Object`, `*> log.txt`,
# CI log capture), every line a child process writes to stderr gets wrapped in an ErrorRecord; and build_fdb.ps1 /
# flash_*.ps1 all set $ErrorActionPreference='Stop', so that hint line turns into a terminating error
# and the script aborts on the spot. Yet the tools called here all love writing to stderr — FlashDB in pack.py prints hints,
# and mpcli.exe echoes its command line and version on every run (184 B measured). The result is "fine when run directly,
# blows up as soon as it is logged", with only a `>` between the two.
#
# Fix: capture stderr explicitly with 2>&1 (no longer relying on outer redirection), relax EAP to
# Continue for the duration so nothing terminates, then turn ErrorRecords back into plain text lines —
# keeping the information without turning it into glaring red text.
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
    # mpcli looks for fw\ and config\ under its own directory, so it must be launched from there.
    Push-Location (Split-Path -Parent $Mpcli)
    try {
        Invoke-Native -Exe $Mpcli -Arguments $Arguments
        if ($LASTEXITCODE -ne 0) { throw "mpcli 失败 (exit $LASTEXITCODE)" }
    }
    finally { Pop-Location }
}
