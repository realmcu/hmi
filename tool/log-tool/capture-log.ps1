param(
    [string]$Port = "COM3",
    [int]$Baud = 2000000,
    [string]$RuntimeTraceProfile = "RTL8773G\v1.14.3.2"
)

$ErrorActionPreference = "Stop"

$toolSource = Join-Path $PSScriptRoot "DebugAnalyzer"
$appTraceSource = Join-Path $PSScriptRoot "..\..\..\applications\bin\app.trace"
$runtimeTraceSource = Join-Path (Join-Path $PSScriptRoot "runtime-traces") $RuntimeTraceProfile
$traceManifestPath = Join-Path $runtimeTraceSource "manifest.json"

$localRoot = Join-Path $env:USERPROFILE ".rtl87-flash\log-tool"
$localTool = Join-Path $localRoot "DebugAnalyzer"
$localTrace = Join-Path $localRoot "trace"
$localOutput = Join-Path $localRoot "output"

New-Item -ItemType Directory -Force $localTool, $localTrace, $localOutput | Out-Null

Copy-Item -Path (Join-Path $toolSource "*") -Destination $localTool -Force

if (-not (Test-Path -LiteralPath $appTraceSource)) {
    throw "App trace not found: $appTraceSource. Run scripts/build.sh first."
}
if (-not (Test-Path -LiteralPath $traceManifestPath)) {
    throw "Runtime trace manifest not found: $traceManifestPath"
}

$traceManifest = Get-Content -LiteralPath $traceManifestPath -Raw | ConvertFrom-Json
$traceFiles = @($traceManifest.files.PSObject.Properties.Name)

function Get-RuntimeTraceEvidence {
    param(
        [string]$LogPath,
        [pscustomobject]$TraceManifest
    )

    $logText = Get-Content -LiteralPath $LogPath -Raw
    $bootRomMatch = [regex]::Match($logText, "BB2Ultra Boot ROM:\s*(0x[0-9a-fA-F]+)")
    $stackPatchMatch = [regex]::Match(
        $logText,
        "Stack Patch git_ver:([0-9.]+), commit-id:([0-9a-fA-F]+)")
    if (-not $bootRomMatch.Success -or -not $stackPatchMatch.Success) {
        return "NO_BOOT_MARKERS"
    }

    $evidence = $TraceManifest.runtime_evidence
    $bootRom = $bootRomMatch.Groups[1].Value.ToLowerInvariant()
    $stackPatchVersion = $stackPatchMatch.Groups[1].Value
    $stackPatchCommit = $stackPatchMatch.Groups[2].Value.ToLowerInvariant()
    $expectedBootRom = $evidence.boot_rom.ToLowerInvariant()
    $expectedStackPatchVersion = $evidence.stack_patch_version
    $expectedStackPatchCommit = $evidence.stack_patch_commit.ToLowerInvariant()
    $hasBootPatchToken = $logText.Contains($evidence.boot_patch_decode_token)
    $hasUpperstackToken = $logText.Contains($evidence.upperstack_init_token)

    if (($bootRom -ne $expectedBootRom) -or
        ($stackPatchVersion -ne $expectedStackPatchVersion) -or
        ($stackPatchCommit -ne $expectedStackPatchCommit) -or
        (-not $hasBootPatchToken) -or
        (-not $hasUpperstackToken)) {
        throw ("Trace/runtime evidence mismatch: boot_rom=$bootRom " +
            "stack_patch=$stackPatchVersion/$stackPatchCommit " +
            "boot_patch_token=$hasBootPatchToken upperstack_token=$hasUpperstackToken")
    }

    return "OK"
}

foreach ($name in $traceFiles) {
    $sourcePath = Join-Path $runtimeTraceSource $name
    if (-not (Test-Path -LiteralPath $sourcePath)) {
        throw "Runtime trace file not found: $sourcePath"
    }

    $expectedHash = $traceManifest.files.$name.ToLowerInvariant()
    $actualHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "Runtime trace hash mismatch: $name expected=$expectedHash actual=$actualHash"
    }

    Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $localTrace $name) -Force
}
Copy-Item -Path $appTraceSource -Destination (Join-Path $localTrace "app.trace") -Force

$arguments = @(
    "-j0",
    "-fs",
    "-sp", $Port,
    "-br", "$Baud",
    "-ar", (Join-Path $localTrace "ROM.trace"),
    "-ri", (Join-Path $localTrace "LOG1.h"),
    "-apf", (Join-Path $localTrace "app.trace"),
    "-aus", (Join-Path $localTrace "upperstack.trace"),
    "-aa", (Join-Path $localTrace "app.trace"),
    "-ap", (Join-Path $localTrace "boot_patch0.trace"),
    "-bpt", (Join-Path $localTrace "boot_patch0.trace"),
    "-pi", (Join-Path $localTrace "patch_log.h"),
    "-spi", (Join-Path $localTrace "stack_patch_log.h"),
    "-da", (Join-Path $localTrace "DSP_APP.trace"),
    "-du", (Join-Path $localTrace "DSP_SDK_AUDIO.trace"),
    "-dv", (Join-Path $localTrace "DSP_SDK_VOICE.trace"),
    "-fp", $localOutput
)

$process = Start-Process `
    -FilePath (Join-Path $localTool "DebugAnalyzer.exe") `
    -ArgumentList $arguments `
    -WorkingDirectory $localTool `
    -NoNewWindow `
    -PassThru

Write-Host ("LOGTOOL_PID={0}" -f $process.Id)
Write-Host ("OUTPUT={0}" -f $localOutput)
Write-Host ("RUNTIME_TRACE_PROFILE={0}" -f $RuntimeTraceProfile)
Write-Host ("RUNTIME_RELEASE={0}" -f $traceManifest.release)

$startedAt = Get-Date
$runtimeEvidenceDone = $false
$nextEvidenceCheck = [datetime]::MinValue
try {
    while (-not $process.HasExited) {
        Start-Sleep -Milliseconds 500
        if (-not $runtimeEvidenceDone -and [datetime]::UtcNow -ge $nextEvidenceCheck) {
            $nextEvidenceCheck = [datetime]::UtcNow.AddSeconds(1)
            $currentLog = Get-ChildItem -LiteralPath $localOutput -Filter "*.log" |
                Where-Object { $_.LastWriteTime -ge $startedAt.AddSeconds(-2) } |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1
            if ($currentLog -ne $null) {
                $evidenceStatus = Get-RuntimeTraceEvidence `
                    -LogPath $currentLog.FullName `
                    -TraceManifest $traceManifest
                if ($evidenceStatus -eq "OK") {
                    Write-Host "TRACE_EVIDENCE=OK"
                    $runtimeEvidenceDone = $true
                }
            }
        }
    }
} finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ("LOGTOOL_EXIT={0}" -f $process.ExitCode)
Write-Host ("LOGS_DIR={0}" -f $localOutput)

$latestLog = Get-ChildItem -LiteralPath $localOutput -Filter "*.log" |
    Where-Object { $_.LastWriteTime -ge $startedAt.AddSeconds(-2) } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $runtimeEvidenceDone -and $latestLog -ne $null) {
    $evidenceStatus = Get-RuntimeTraceEvidence `
        -LogPath $latestLog.FullName `
        -TraceManifest $traceManifest
    Write-Host ("TRACE_EVIDENCE={0}" -f $evidenceStatus)
}
