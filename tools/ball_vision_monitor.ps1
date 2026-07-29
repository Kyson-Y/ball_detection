[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [ValidateRange(3, 900)]
    [int]$DurationSeconds = 15,
    [ValidateRange(0, 10)]
    [int]$FlushSeconds = 1,
    [ValidateNotNullOrEmpty()]
    [string]$CameraStatusUri = "http://10.5.66.1:8080/status.json",
    [ValidateRange(100, 2000)]
    [int]$CameraPollIntervalMs = 250,
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDirectory = Join-Path $projectRoot "tmp\ball-vision-$stamp"
}
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

$cameraCsvPath = Join-Path $outputRoot "camera_status.csv"
$cameraJsonPath = Join-Path $outputRoot "camera_summary.json"
$mcuCsvPath = Join-Path $outputRoot "mcu_ball_balance.csv"
$mcuJsonPath = Join-Path $outputRoot "mcu_summary.json"
$mcuStdoutPath = Join-Path $outputRoot "mcu_capture.log"
$mcuStderrPath = Join-Path $outputRoot "mcu_capture.err.log"
$combinedJsonPath = Join-Path $outputRoot "combined_summary.json"

$cameraCaptureSeconds = $DurationSeconds + $FlushSeconds
$cameraJob = Start-Job -ArgumentList @(
    $CameraStatusUri,
    $cameraCaptureSeconds,
    $CameraPollIntervalMs,
    $cameraCsvPath,
    $cameraJsonPath
) -ScriptBlock {
    param(
        [string]$Uri,
        [int]$Seconds,
        [int]$PollIntervalMs,
        [string]$CsvPath,
        [string]$JsonPath
    )

    $culture = [System.Globalization.CultureInfo]::InvariantCulture
    $writer = [System.IO.StreamWriter]::new(
        $CsvPath,
        $false,
        [System.Text.UTF8Encoding]::new($false)
    )
    $writer.WriteLine(
        "host_time_utc,elapsed_ms,http_latency_ms,control_hz,uart_hz," +
        "detected,reference_mismatch,position_mm,velocity_mm_s," +
        "velocity_valid,confidence,temperature_c,flags,uart_errors,frames"
    )

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $samples = 0
    $errors = 0
    $firstSampleMs = $null
    $lastSampleMs = $null
    $firstFrames = $null
    $lastFrames = $null
    $controlRateSum = 0.0
    $uartRateSum = 0.0
    $lastSnapshot = $null
    $lastError = ""

    try {
        while ($stopwatch.Elapsed.TotalSeconds -lt $Seconds) {
            $iterationStartedMs = $stopwatch.Elapsed.TotalMilliseconds
            $requestTimer = [System.Diagnostics.Stopwatch]::StartNew()
            try {
                $snapshot = Invoke-RestMethod -Uri $Uri -Method Get `
                    -TimeoutSec 1 -Headers @{ "Cache-Control" = "no-cache" }
                $requestTimer.Stop()
                $elapsedMs = [Math]::Round(
                    $stopwatch.Elapsed.TotalMilliseconds, 3)
                $hostTime = [DateTime]::UtcNow.ToString("o")
                $temperature = if ($null -eq $snapshot.temperature_c) {
                    ""
                }
                else {
                    ([double]$snapshot.temperature_c).ToString("R", $culture)
                }
                $values = @(
                    $hostTime,
                    $elapsedMs.ToString("R", $culture),
                    $requestTimer.Elapsed.TotalMilliseconds.ToString(
                        "R", $culture),
                    ([double]$snapshot.control_hz).ToString("R", $culture),
                    ([double]$snapshot.uart_hz).ToString("R", $culture),
                    [int][bool]$snapshot.detected,
                    [int][bool]$snapshot.reference_mismatch,
                    ([double]$snapshot.position_mm).ToString("R", $culture),
                    ([double]$snapshot.velocity_mm_s).ToString("R", $culture),
                    [int][bool]$snapshot.velocity_valid,
                    ([double]$snapshot.confidence).ToString("R", $culture),
                    $temperature,
                    [int]$snapshot.flags,
                    [int]$snapshot.uart_errors,
                    [uint64]$snapshot.frames
                )
                $writer.WriteLine($values -join ",")

                $samples++
                if ($null -eq $firstSampleMs) {
                    $firstSampleMs = $elapsedMs
                    $firstFrames = [uint64]$snapshot.frames
                }
                $lastSampleMs = $elapsedMs
                $lastFrames = [uint64]$snapshot.frames
                $controlRateSum += [double]$snapshot.control_hz
                $uartRateSum += [double]$snapshot.uart_hz
                $lastSnapshot = $snapshot
            }
            catch {
                $requestTimer.Stop()
                $errors++
                $lastError = $_.Exception.Message
            }

            $iterationMs = $stopwatch.Elapsed.TotalMilliseconds -
                $iterationStartedMs
            $remainingMs = $PollIntervalMs - $iterationMs
            if ($remainingMs -gt 0.0) {
                Start-Sleep -Milliseconds ([int][Math]::Ceiling($remainingMs))
            }
        }
    }
    finally {
        $stopwatch.Stop()
        $writer.Dispose()
    }

    $observedSeconds = if (($samples -gt 1) -and
        ($lastSampleMs -gt $firstSampleMs)) {
        ($lastSampleMs - $firstSampleMs) / 1000.0
    }
    else {
        0.0
    }
    $summary = [pscustomobject]@{
        Uri = $Uri
        DurationSeconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
        Samples = $samples
        Errors = $errors
        EffectiveStatusRateHz = if ($observedSeconds -gt 0.0) {
            [Math]::Round(($samples - 1) / $observedSeconds, 3)
        } else { 0.0 }
        EffectiveCameraFrameRateHz = if ($observedSeconds -gt 0.0) {
            [Math]::Round(($lastFrames - $firstFrames) / $observedSeconds, 3)
        } else { 0.0 }
        AverageControlRateHz = if ($samples -gt 0) {
            [Math]::Round($controlRateSum / $samples, 3)
        } else { 0.0 }
        AverageUartRateHz = if ($samples -gt 0) {
            [Math]::Round($uartRateSum / $samples, 3)
        } else { 0.0 }
        LastSnapshot = $lastSnapshot
        LastError = $lastError
        CsvPath = $CsvPath
    }
    $summary | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath $JsonPath -Encoding UTF8
    $summary
}

$telemetryScript = Join-Path $PSScriptRoot "telemetry_capture.ps1"
$powershellExe = (Get-Command powershell.exe -ErrorAction Stop).Source
$mcuArguments = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", ('"{0}"' -f $telemetryScript),
    "-Port", $Port,
    "-BaudRate", $BaudRate,
    "-DurationSeconds", $DurationSeconds,
    "-FlushSeconds", $FlushSeconds,
    "-BallBalanceCsvPath", ('"{0}"' -f $mcuCsvPath),
    "-JsonPath", ('"{0}"' -f $mcuJsonPath)
)

try {
    $mcuProcess = Start-Process -FilePath $powershellExe `
        -ArgumentList $mcuArguments -Wait -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $mcuStdoutPath `
        -RedirectStandardError $mcuStderrPath
}
finally {
    Wait-Job -Job $cameraJob | Out-Null
    Receive-Job -Job $cameraJob | Out-Null
    Remove-Job -Job $cameraJob -Force
}

if (Test-Path -LiteralPath $mcuStdoutPath) {
    Get-Content -LiteralPath $mcuStdoutPath | Out-Host
}
if ((Test-Path -LiteralPath $mcuStderrPath) -and
    ((Get-Item -LiteralPath $mcuStderrPath).Length -gt 0)) {
    Get-Content -LiteralPath $mcuStderrPath | Write-Warning
}

$cameraSummary = if (Test-Path -LiteralPath $cameraJsonPath) {
    Get-Content -Raw -LiteralPath $cameraJsonPath | ConvertFrom-Json
} else { $null }
$mcuSummary = if (Test-Path -LiteralPath $mcuJsonPath) {
    Get-Content -Raw -LiteralPath $mcuJsonPath | ConvertFrom-Json
} else { $null }

$combined = [pscustomobject]@{
    StartedAtLocal = (Get-Date).ToString("o")
    OutputDirectory = $outputRoot
    Camera = $cameraSummary
    Mcu = $mcuSummary
    McuCaptureExitCode = $mcuProcess.ExitCode
}
$combined | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $combinedJsonPath -Encoding UTF8

$cameraOk = ($null -ne $cameraSummary) -and
    ([int]$cameraSummary.Samples -gt 0)
$mcuOk = ($mcuProcess.ExitCode -eq 0) -and
    ($null -ne $mcuSummary) -and
    ([int]$mcuSummary.BallBalanceFrames -gt 0)

[pscustomobject]@{
    OutputDirectory = $outputRoot
    CameraSamples = if ($null -ne $cameraSummary) {
        $cameraSummary.Samples
    } else { 0 }
    CameraFrameRateHz = if ($null -ne $cameraSummary) {
        $cameraSummary.EffectiveCameraFrameRateHz
    } else { 0.0 }
    McuBallBalanceFrames = if ($null -ne $mcuSummary) {
        $mcuSummary.BallBalanceFrames
    } else { 0 }
    McuBallBalanceRateHz = if ($null -ne $mcuSummary) {
        $mcuSummary.BallBalanceRateHz
    } else { 0.0 }
    CameraOk = $cameraOk
    McuOk = $mcuOk
} | Format-List

if (-not $cameraOk -or -not $mcuOk) {
    exit 2
}
