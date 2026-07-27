[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [ValidateRange(-90.0, 90.0)]
    [double]$BaseRpm = 12.0,
    [ValidateRange(-360.0, 360.0)]
    [double]$HeadingDeltaDeg = 10.0,
    [ValidateRange(2, 15)]
    [int]$DurationSeconds = 4,
    [string]$OutputDirectory = "",
    [switch]$ConfirmUserPresent,
    [switch]$ConfirmWheelSuspended,
    [switch]$ConfirmGroundClear,
    [switch]$ConfirmBothMotorsConnected,
    [switch]$ConfirmBatteryPowered,
    [switch]$ConfirmPhysicalDisconnectReady
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

if ([Math]::Abs($BaseRpm) -gt 0.05 -and
    [Math]::Abs($BaseRpm) -lt 8.0) {
    throw "BaseRpm must be zero for a pivot or at least 8 rpm."
}
if (-not $ConfirmUserPresent -or
    (-not $ConfirmWheelSuspended -and -not $ConfirmGroundClear) -or
    -not $ConfirmBothMotorsConnected -or -not $ConfirmBatteryPowered -or
    -not $ConfirmPhysicalDisconnectReady) {
    throw "Heading tests require a suspended wheel or clear ground area and all physical safety confirmations."
}

function Get-Crc16Ccitt {
    param([byte[]]$Data, [int]$Offset, [int]$Length)

    [uint32]$crc = 0xFFFF
    for ($index = 0; $index -lt $Length; $index++) {
        $crc = $crc -bxor ([uint32]$Data[$Offset + $index] -shl 8)
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 0x8000) -ne 0) {
                $crc = (($crc -shl 1) -bxor 0x1021) -band 0xFFFF
            } else {
                $crc = ($crc -shl 1) -band 0xFFFF
            }
        }
    }
    return [uint16]$crc
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $EchoPaths.ProjectRoot `
        "tests\artifacts\heading-$timestamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$rawPath = Join-Path $OutputDirectory "capture.bin"
$controlCsvPath = Join-Path $OutputDirectory "control.csv"
$attitudeCsvPath = Join-Path $OutputDirectory "attitude.csv"
$jsonPath = Join-Path $OutputDirectory "capture.json"
$resultPath = Join-Path $OutputDirectory "heading-result.json"

[uint32]$sequence = Get-Random -Minimum 1 -Maximum 2000000000
[int16]$baseDeciRpm = [Math]::Round(
    $BaseRpm * 10.0, [MidpointRounding]::AwayFromZero)
[int16]$headingDeciDeg = [Math]::Round(
    $HeadingDeltaDeg * 10.0, [MidpointRounding]::AwayFromZero)
[uint16]$durationMs = $DurationSeconds * 1000
$command = New-Object byte[] 36
$command[0] = 0xA5
$command[1] = 0x5A
$command[2] = 1
$command[3] = 5
[BitConverter]::GetBytes([uint16]20).CopyTo($command, 4)
[BitConverter]::GetBytes([uint32]1).CopyTo($command, 6)
[BitConverter]::GetBytes([uint32]0).CopyTo($command, 10)
[BitConverter]::GetBytes(
    [Convert]::ToUInt32("4543484F", 16)).CopyTo($command, 14)
[BitConverter]::GetBytes(
    [Convert]::ToUInt32("BABCB7B0", 16)).CopyTo($command, 18)
[BitConverter]::GetBytes($sequence).CopyTo($command, 22)
[BitConverter]::GetBytes($baseDeciRpm).CopyTo($command, 26)
[BitConverter]::GetBytes($headingDeciDeg).CopyTo($command, 28)
[BitConverter]::GetBytes($durationMs).CopyTo($command, 30)
[BitConverter]::GetBytes([uint16]2).CopyTo($command, 32)
$crc = Get-Crc16Ccitt -Data $command -Offset 2 -Length 32
[BitConverter]::GetBytes($crc).CopyTo($command, 34)

$captureSeconds = $DurationSeconds + 4
$serial = [System.IO.Ports.SerialPort]::new(
    $Port, $BaudRate, [System.IO.Ports.Parity]::None, 8,
    [System.IO.Ports.StopBits]::One)
$serial.ReadBufferSize = 1MB
$serial.ReadTimeout = 100
$capture = [System.IO.MemoryStream]::new($captureSeconds * 16384)
$buffer = New-Object byte[] 4096
$sent = $false
try {
    $serial.Open()
    $serial.DiscardInBuffer()
    Start-Sleep -Milliseconds 100
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $captureSeconds) {
        if (-not $sent -and $watch.Elapsed.TotalMilliseconds -ge 500) {
            $serial.Write($command, 0, $command.Length)
            $sent = $true
        }
        while ($serial.BytesToRead -gt 0) {
            $count = [Math]::Min($serial.BytesToRead, $buffer.Length)
            $read = $serial.Read($buffer, 0, $count)
            $capture.Write($buffer, 0, $read)
        }
        Start-Sleep -Milliseconds 2
    }
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
}
if (-not $sent) {
    throw "The heading command was not sent."
}
[System.IO.File]::WriteAllBytes($rawPath, $capture.ToArray())
$capture.Dispose()

$captureTool = Join-Path $PSScriptRoot "telemetry_capture.ps1"
& $captureTool -InputPath $rawPath -CsvPath $controlCsvPath `
    -AttitudeCsvPath $attitudeCsvPath -JsonPath $jsonPath | Out-Host
$parserExitCode = $LASTEXITCODE
$summary = Get-Content -Raw $jsonPath | ConvertFrom-Json
$rows = @(Import-Csv $controlCsvPath)
$headingRows = @($rows | Where-Object {
    (([uint32]$_.flags -band 256) -ne 0)
})
$trackingRows = @($headingRows | Where-Object {
    (([uint32]$_.flags -band 128) -ne 0)
})
$attitudeRows = @(Import-Csv $attitudeCsvPath)
$motionAttitudeRows = if ($headingRows.Count -gt 0) {
    [uint32]$motionStartUs = $headingRows[0].timestamp_us
    [uint32]$motionEndUs = $headingRows[-1].timestamp_us
    @($attitudeRows | Where-Object {
        ([uint32]$_.timestamp_us -ge $motionStartUs) -and
        ([uint32]$_.timestamp_us -le $motionEndUs)
    })
} else { @() }
$tail = @($trackingRows | Select-Object -Last 100)
$targetDifference = if ($tail.Count -gt 0) {
    ($tail | ForEach-Object {
        [double]$_.right_setpoint - [double]$_.setpoint
    } | Measure-Object -Average).Average
} else { 0.0 }
$measuredDifference = if ($tail.Count -gt 0) {
    ($tail | ForEach-Object {
        [double]$_.control_output - [double]$_.measurement
    } | Measure-Object -Average).Average
} else { 0.0 }
$expectedSign = [Math]::Sign($HeadingDeltaDeg)
$straightMode = [Math]::Abs($HeadingDeltaDeg) -lt 0.05
$startYawDeg = if ($motionAttitudeRows.Count -gt 0) {
    [double]$motionAttitudeRows[0].yaw_deg
} else { 0.0 }
$endYawDeg = if ($motionAttitudeRows.Count -gt 0) {
    [double]$motionAttitudeRows[-1].yaw_deg
} else { 0.0 }
$achievedDeltaDeg = 0.0
for ($index = 1; $index -lt $motionAttitudeRows.Count; $index++) {
    $yawStepDeg = [double]$motionAttitudeRows[$index].yaw_deg -
        [double]$motionAttitudeRows[$index - 1].yaw_deg
    while ($yawStepDeg -gt 180.0) { $yawStepDeg -= 360.0 }
    while ($yawStepDeg -lt -180.0) { $yawStepDeg += 360.0 }
    $achievedDeltaDeg += $yawStepDeg
}
$finalHeadingErrorDeg = $HeadingDeltaDeg - $achievedDeltaDeg
while ($finalHeadingErrorDeg -gt 180.0) { $finalHeadingErrorDeg -= 360.0 }
while ($finalHeadingErrorDeg -lt -180.0) { $finalHeadingErrorDeg += 360.0 }
$directionValid = $straightMode -or
    ([Math]::Sign($achievedDeltaDeg) -eq $expectedSign)
$headingErrorValid = ($motionAttitudeRows.Count -gt 0) -and
    ([Math]::Abs($finalHeadingErrorDeg) -le 2.0)
$ack = $summary.LatestActuatorAck
$health = $summary.LatestHealth
$ackValid = ($null -ne $ack) -and
    ([uint32]$ack.Sequence -eq $sequence) -and
    ([int]$ack.Status -eq 0) -and ([int]$ack.Reserved -eq 2)
$healthClean = ($null -ne $health) -and
    ($health.ActuatorOutputPermitted -eq 0) -and
    ($health.ActiveIssueMask -eq "0x00000000") -and
    ($health.StickyIssueMask -eq "0x00000000") -and
    ($health.DeadlineMissCount -eq 0) -and
    ($health.I2cErrorCount -eq 0)
$resultPassed = $ackValid -and $healthClean -and
    ($headingRows.Count -ge $DurationSeconds * 80) -and
    ($trackingRows.Count -gt 0) -and $directionValid -and
    $headingErrorValid -and
    ($summary.AttitudeSampleRateHz -ge 98) -and
    ($summary.AttitudeSampleRateHz -le 102)
$result = [pscustomobject]@{
    Result = if ($resultPassed) { "passed" } else { "failed" }
    BaseRpm = $BaseRpm
    HeadingDeltaDeg = $HeadingDeltaDeg
    DurationSeconds = $DurationSeconds
    Sequence = $sequence
    AckValid = $ackValid
    HeadingFrames = $headingRows.Count
    TrackingFrames = $trackingRows.Count
    TargetDifferenceRpm = [Math]::Round($targetDifference, 3)
    MeasuredDifferenceRpm = [Math]::Round($measuredDifference, 3)
    DirectionValid = $directionValid
    HeadingErrorValid = $headingErrorValid
    StartYawDeg = [Math]::Round($startYawDeg, 3)
    EndYawDeg = [Math]::Round($endYawDeg, 3)
    AchievedDeltaDeg = [Math]::Round($achievedDeltaDeg, 3)
    FinalHeadingErrorDeg = [Math]::Round($finalHeadingErrorDeg, 3)
    AttitudeSampleRateHz = $summary.AttitudeSampleRateHz
    ControlRateHz = $summary.ControlRateHz
    ParserExitCode = $parserExitCode
    HostCrcErrors = $summary.CrcErrors
    HostSequenceGaps = $summary.SequenceGaps
    HealthClean = $healthClean
    OutputDirectory = $OutputDirectory
}
$result | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8
$result | Format-List
if (-not $resultPassed) {
    exit 2
}
