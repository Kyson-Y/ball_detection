[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [Parameter(Mandatory = $true)]
    [ValidateRange(-90.0, 90.0)]
    [double]$SpeedRpm,
    [Parameter(Mandatory = $true)]
    [ValidateRange(20, 5000)]
    [int]$DistanceMm,
    [ValidateRange(2, 60)]
    [int]$TimeoutSeconds = 15,
    [string]$OutputDirectory = "",
    [switch]$ConfirmUserPresent,
    [switch]$ConfirmGroundClear,
    [switch]$ConfirmBothMotorsConnected,
    [switch]$ConfirmBatteryPowered,
    [switch]$ConfirmPhysicalDisconnectReady
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

if ([Math]::Abs($SpeedRpm) -lt 8.0) {
    throw "SpeedRpm must have magnitude of at least 8 rpm."
}
if (-not $ConfirmUserPresent -or -not $ConfirmGroundClear -or
    -not $ConfirmBothMotorsConnected -or -not $ConfirmBatteryPowered -or
    -not $ConfirmPhysicalDisconnectReady) {
    throw "Distance tests require clear ground and all safety confirmations."
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
        "tests\artifacts\distance-$timestamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$rawPath = Join-Path $OutputDirectory "capture.bin"
$controlCsvPath = Join-Path $OutputDirectory "control.csv"
$attitudeCsvPath = Join-Path $OutputDirectory "attitude.csv"
$jsonPath = Join-Path $OutputDirectory "capture.json"
$resultPath = Join-Path $OutputDirectory "distance-result.json"

[uint32]$sequence = Get-Random -Minimum 1 -Maximum 2000000000
[int16]$speedDeciRpm = [Math]::Round(
    $SpeedRpm * 10.0, [MidpointRounding]::AwayFromZero)
[int16]$distanceCommandMm = $DistanceMm
[uint16]$durationMs = $TimeoutSeconds * 1000
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
[BitConverter]::GetBytes($speedDeciRpm).CopyTo($command, 26)
[BitConverter]::GetBytes($distanceCommandMm).CopyTo($command, 28)
[BitConverter]::GetBytes($durationMs).CopyTo($command, 30)
[BitConverter]::GetBytes([uint16]3).CopyTo($command, 32)
$crc = Get-Crc16Ccitt -Data $command -Offset 2 -Length 32
[BitConverter]::GetBytes($crc).CopyTo($command, 34)

$captureSeconds = $TimeoutSeconds + 4
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
    throw "The distance command was not sent."
}
[System.IO.File]::WriteAllBytes($rawPath, $capture.ToArray())
$capture.Dispose()

$captureTool = Join-Path $PSScriptRoot "telemetry_capture.ps1"
& $captureTool -InputPath $rawPath -CsvPath $controlCsvPath `
    -AttitudeCsvPath $attitudeCsvPath -JsonPath $jsonPath | Out-Host
$parserExitCode = $LASTEXITCODE
$summary = Get-Content -Raw $jsonPath | ConvertFrom-Json
$rows = @(Import-Csv $controlCsvPath)
$distanceRows = @($rows | Where-Object {
    (([uint32]$_.flags -band 512) -ne 0)
})
$attitudeRows = @(Import-Csv $attitudeCsvPath)

$leftDistanceMm = 0.0
$rightDistanceMm = 0.0
for ($index = 1; $index -lt $distanceRows.Count; $index++) {
    $periodS = ([double]$distanceRows[$index].timestamp_us -
        [double]$distanceRows[$index - 1].timestamp_us) / 1000000.0
    if ($periodS -gt 0.0 -and $periodS -lt 0.2) {
        $leftRpm = 0.5 * ([double]$distanceRows[$index - 1].measurement +
            [double]$distanceRows[$index].measurement)
        $rightRpm = 0.5 * ([double]$distanceRows[$index - 1].control_output +
            [double]$distanceRows[$index].control_output)
        $leftDistanceMm += $leftRpm * ([Math]::PI * 65.0) * $periodS / 60.0
        $rightDistanceMm += $rightRpm * ([Math]::PI * 65.0) * $periodS / 60.0
    }
}
$direction = [Math]::Sign($SpeedRpm)
$leftDistanceMm *= $direction
$rightDistanceMm *= $direction
$centerDistanceMm = 0.5 * ($leftDistanceMm + $rightDistanceMm)

$motionAttitudeRows = if ($distanceRows.Count -gt 0) {
    [uint32]$motionStartUs = $distanceRows[0].timestamp_us
    [uint32]$motionEndUs = $distanceRows[-1].timestamp_us
    @($attitudeRows | Where-Object {
        ([uint32]$_.timestamp_us -ge $motionStartUs) -and
        ([uint32]$_.timestamp_us -le $motionEndUs)
    })
} else { @() }
$headingDeltaDeg = 0.0
for ($index = 1; $index -lt $motionAttitudeRows.Count; $index++) {
    $step = [double]$motionAttitudeRows[$index].yaw_deg -
        [double]$motionAttitudeRows[$index - 1].yaw_deg
    while ($step -gt 180.0) { $step -= 360.0 }
    while ($step -lt -180.0) { $step += 360.0 }
    $headingDeltaDeg += $step
}

$lastDistanceIndex = -1
for ($index = 0; $index -lt $rows.Count; $index++) {
    if ((([uint32]$rows[$index].flags -band 512) -ne 0)) {
        $lastDistanceIndex = $index
    }
}
$trailingSafeFrames = if ($lastDistanceIndex -ge 0) {
    $rows.Count - $lastDistanceIndex - 1
} else { 0 }
$activeDurationS = if ($distanceRows.Count -gt 1) {
    ([double]$distanceRows[-1].timestamp_us -
        [double]$distanceRows[0].timestamp_us) / 1000000.0
} else { 0.0 }
$ack = $summary.LatestActuatorAck
$health = $summary.LatestHealth
$ackValid = ($null -ne $ack) -and
    ([uint32]$ack.Sequence -eq $sequence) -and
    ([int]$ack.Status -eq 0) -and ([int]$ack.Reserved -eq 3) -and
    ([int]$ack.LeftElectricalPermille -eq [int]$speedDeciRpm) -and
    ([int]$ack.RightElectricalPermille -eq $DistanceMm)
$healthClean = ($null -ne $health) -and
    ($health.ActuatorOutputPermitted -eq 0) -and
    ($health.ActiveIssueMask -eq "0x00000000") -and
    ($health.StickyIssueMask -eq "0x00000000") -and
    ($health.DeadlineMissCount -eq 0) -and
    ($health.I2cErrorCount -eq 0)
$distanceValid = $centerDistanceMm -ge ($DistanceMm - 25.0) -and
    $centerDistanceMm -le ($DistanceMm + 80.0)
$stoppedBeforeTimeout = $activeDurationS -gt 0.0 -and
    $activeDurationS -lt ($TimeoutSeconds - 0.1)
$resultPassed = $ackValid -and $healthClean -and
    ($distanceRows.Count -gt 10) -and ($trailingSafeFrames -ge 10) -and
    $distanceValid -and $stoppedBeforeTimeout -and
    ([Math]::Abs($headingDeltaDeg) -le 2.0)
$result = [pscustomobject]@{
    Result = if ($resultPassed) { "passed" } else { "failed" }
    SpeedRpm = $SpeedRpm
    TargetDistanceMm = $DistanceMm
    EstimatedLeftDistanceMm = [Math]::Round($leftDistanceMm, 2)
    EstimatedRightDistanceMm = [Math]::Round($rightDistanceMm, 2)
    EstimatedCenterDistanceMm = [Math]::Round($centerDistanceMm, 2)
    HeadingDeltaDeg = [Math]::Round($headingDeltaDeg, 3)
    ActiveDurationS = [Math]::Round($activeDurationS, 3)
    TimeoutSeconds = $TimeoutSeconds
    StoppedBeforeTimeout = $stoppedBeforeTimeout
    DistanceFrames = $distanceRows.Count
    TrailingSafeFrames = $trailingSafeFrames
    AckValid = $ackValid
    HealthClean = $healthClean
    ParserExitCode = $parserExitCode
    HostCrcErrors = $summary.CrcErrors
    HostSequenceGaps = $summary.SequenceGaps
    OutputDirectory = $OutputDirectory
}
$result | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $resultPath -Encoding UTF8
$result | Format-List
if (-not $resultPassed) {
    exit 2
}
