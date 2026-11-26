param(
    [string]$Port = "COM3",
    [int]$Baud = 2000000
)

$ErrorActionPreference = "Stop"

$toolSource = Join-Path $PSScriptRoot "DebugAnalyzer"
$traceSource = Join-Path $PSScriptRoot "..\..\..\zephyrproject\realtek-app\bin\rtl8773g\trace"
$appTraceSource = Join-Path $PSScriptRoot "..\..\..\applications\bin\app.trace"

$localRoot = Join-Path $env:USERPROFILE ".rtl87-flash\log-tool"
$localTool = Join-Path $localRoot "DebugAnalyzer"
$localTrace = Join-Path $localRoot "trace"
$localOutput = Join-Path $localRoot "output"

New-Item -ItemType Directory -Force $localTool, $localTrace, $localOutput | Out-Null

Copy-Item -Path (Join-Path $toolSource "*") -Destination $localTool -Force

$traceFiles = @(
    "ROM.trace",
    "LOG1.h",
    "upperstack.trace",
    "app_honeygui.trace",
    "boot_patch0.trace",
    "patch_log.h",
    "stack_patch_log.h",
    "DSP_APP.trace",
    "DSP_SDK_AUDIO.trace",
    "DSP_SDK_VOICE.trace"
)

foreach ($name in $traceFiles) {
    Copy-Item -Path (Join-Path $traceSource $name) -Destination (Join-Path $localTrace $name) -Force
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
    "-aa", (Join-Path $localTrace "app_honeygui.trace"),
    "-ap", (Join-Path $localTrace "boot_patch0.trace"),
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

try {
    while (-not $process.HasExited) {
        Start-Sleep -Milliseconds 500
    }
} finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ("LOGTOOL_EXIT={0}" -f $process.ExitCode)
Write-Host ("LOGS_DIR={0}" -f $localOutput)
