[CmdletBinding()]
param(
    [string]$Port = "COM4",
    [ValidateSet("Gen1", "Gen2")]
    [string]$Axis = "Gen2",
    [ValidateRange(10, 400)]
    [int]$RateHz = 200,
    [ValidateRange(-3000, 3000)]
    [int]$SpeedRpm = 70,
    [int[]]$SpeedProfileRpm = @(),
    [ValidateRange(1, 120)]
    [int]$DurationSeconds = 20,
    [ValidateRange(100, 3000)]
    [int]$AccelerationRpmPerSecond = 1000,
    [switch]$ConfirmUserPresent,
    [switch]$ConfirmMechanismSuspended,
    [switch]$ConfirmCurrentLimitedSupply,
    [switch]$ConfirmPhysicalDisconnectReady
)

$ErrorActionPreference = "Stop"
if ($SpeedRpm -eq 0) {
    throw "Use a nonzero speed for the rate demonstration."
}
$profile = if ($SpeedProfileRpm.Count -gt 0) {
    @($SpeedProfileRpm)
} else {
    @($SpeedRpm)
}
foreach ($profileSpeed in $profile) {
    if (($profileSpeed -eq 0) -or ($profileSpeed -lt -3000) -or
        ($profileSpeed -gt 3000)) {
        throw "Each profile speed must be nonzero and within +/-3000 rpm."
    }
}
if (-not $ConfirmUserPresent -or
    -not $ConfirmMechanismSuspended -or
    -not $ConfirmCurrentLimitedSupply -or
    -not $ConfirmPhysicalDisconnectReady) {
    throw "Speed demonstration requires all four safety confirmations."
}

$commandTool = Join-Path $PSScriptRoot "zdt_backup_command.ps1"
$sequence = [uint32](Get-Random -Minimum 1000 -Maximum 2000000000)

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

function Get-U16 {
    param([byte[]]$Data, [int]$Offset)
    return [BitConverter]::ToUInt16($Data, $Offset)
}

function Get-U32 {
    param([byte[]]$Data, [int]$Offset)
    return [BitConverter]::ToUInt32($Data, $Offset)
}

function Get-I16 {
    param([byte[]]$Data, [int]$Offset)
    return [BitConverter]::ToInt16($Data, $Offset)
}

function Get-I32 {
    param([byte[]]$Data, [int]$Offset)
    return [BitConverter]::ToInt32($Data, $Offset)
}

function Get-NextSequence {
    $script:sequence++
    if ($script:sequence -eq 0) {
        $script:sequence = 1
    }
    return $script:sequence
}

function New-ZdtFrame {
    param(
        [uint32]$CommandSequence,
        [ValidateSet("Speed", "Status")]
        [string]$Action,
        [int]$TargetSpeedRpm = $SpeedRpm,
        [bool]$SuppressAck = $false
    )

    $operation = if ($Action -eq "Speed") { 4 } else { 0 }
    $value = if ($Action -eq "Speed") { $TargetSpeedRpm } else { 0 }
    $magic = [Convert]::ToUInt32("5A445442", 16)
    $magicInverse = [Convert]::ToUInt32("A5BBABBD", 16)
    $frame = New-Object byte[] 44
    $frame[0] = 0xA5
    $frame[1] = 0x5A
    $frame[2] = 1
    $frame[3] = 8
    [BitConverter]::GetBytes([uint16]28).CopyTo($frame, 4)
    [BitConverter]::GetBytes($CommandSequence).CopyTo($frame, 6)
    [BitConverter]::GetBytes([uint32]0).CopyTo($frame, 10)
    [BitConverter]::GetBytes($magic).CopyTo($frame, 14)
    [BitConverter]::GetBytes($magicInverse).CopyTo($frame, 18)
    [BitConverter]::GetBytes($CommandSequence).CopyTo($frame, 22)
    $frame[26] = [byte]$operation
    $frame[27] = if ($Axis -eq "Gen1") { 0 } else { 1 }
    $frame[28] = 0
    $frame[29] = if ($SuppressAck) { 1 } else { 0 }
    [BitConverter]::GetBytes([int32]$value).CopyTo($frame, 30)
    [BitConverter]::GetBytes([uint16]0).CopyTo($frame, 34)
    [BitConverter]::GetBytes([uint16]0).CopyTo($frame, 36)
    [BitConverter]::GetBytes(
        [uint32]$AccelerationRpmPerSecond).CopyTo($frame, 38)
    $crc = Get-Crc16Ccitt -Data $frame -Offset 2 -Length 40
    [BitConverter]::GetBytes($crc).CopyTo($frame, 42)
    return $frame
}

function Invoke-ZdtTool {
    param([string]$Action)

    $arguments = @{
        Port = $Port
        Action = $Action
        Axis = $Axis
        CaptureSeconds = 0.5
        PassThru = $true
    }
    if ($Action -eq "Enable") {
        $arguments.ConfirmUserPresent = $true
        $arguments.ConfirmMechanismSuspended = $true
        $arguments.ConfirmCurrentLimitedSupply = $true
        $arguments.ConfirmPhysicalDisconnectReady = $true
    }
    return & $commandTool @arguments
}

function Read-AvailableBytes {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [System.IO.MemoryStream]$Capture,
        [byte[]]$Buffer
    )

    while ($Serial.BytesToRead -gt 0) {
        $count = [Math]::Min($Serial.BytesToRead, $Buffer.Length)
        $read = $Serial.Read($Buffer, 0, $count)
        $Capture.Write($Buffer, 0, $read)
    }
}

function Get-SpeedSamples {
    param(
        [byte[]]$Bytes,
        [hashtable]$StatusTimes,
        [string]$SelectedAxis
    )

    $samples = [System.Collections.Generic.List[object]]::new()
    for ($index = 0; $index + 164 -le $Bytes.Length; $index++) {
        if ($Bytes[$index] -ne 0xA5 -or
            $Bytes[$index + 1] -ne 0x5A -or
            $Bytes[$index + 2] -ne 1 -or
            $Bytes[$index + 3] -ne 9 -or
            (Get-U16 $Bytes ($index + 4)) -ne 148) {
            continue
        }
        $expectedCrc = Get-U16 $Bytes ($index + 162)
        $actualCrc = Get-Crc16Ccitt -Data $Bytes `
            -Offset ($index + 2) -Length 160
        if ($expectedCrc -ne $actualCrc) {
            continue
        }
        $ackSequence = Get-U32 $Bytes ($index + 14)
        if (-not $StatusTimes.ContainsKey($ackSequence)) {
            continue
        }
        $axisOffset = if ($SelectedAxis -eq "Gen1") { 38 } else { 78 }
        $sample = [ordered]@{
            TimeSeconds = $StatusTimes[$ackSequence].TimeSeconds
            SpeedRpm = Get-I16 $Bytes ($index + $axisOffset + 28)
            PositionMillidegrees = Get-I32 $Bytes ($index + $axisOffset + 24)
            MotorStatusFlags = $Bytes[$index + $axisOffset + 34]
        }
        if ($SelectedAxis -eq "Gen2") {
            $sample.BusVoltageMv = Get-U16 $Bytes ($index + 118)
            $sample.PhaseCurrentMa = Get-U16 $Bytes ($index + 120)
            $sample.EncoderLinear = Get-U16 $Bytes ($index + 122)
            $sample.PositionErrorCounts = Get-I32 $Bytes ($index + 128)
            $sample.HomingStatusFlags = $Bytes[$index + 132]
        }
        $sample.TargetSpeedRpm = $StatusTimes[$ackSequence].TargetSpeedRpm
        $sample.StageIndex = $StatusTimes[$ackSequence].StageIndex
        $sample.StageElapsedSeconds =
            $StatusTimes[$ackSequence].StageElapsedSeconds
        $samples.Add([pscustomobject]$sample)
    }
    return $samples
}

$baseline = $null
$runBaseline = $null
$stopSnapshot = $null
$finalSnapshot = $null
$capture = $null
$serial = $null
$motionSent = 0
$hostSkipped = 0
$capturedBytes = [byte[]]@()

try {
    Invoke-ZdtTool -Action "Select" | Out-Null
    Start-Sleep -Milliseconds 700
    $baseline = Invoke-ZdtTool -Action "Status"
    $onlineProperty = "${Axis}Online"
    if (-not $baseline.$onlineProperty) {
        throw "$Axis did not respond to the online check."
    }
    if ($Axis -eq "Gen2" -and
        (-not $baseline.Gen2SystemStatusValid -or
         -not $baseline.Gen2DriverConfigValid)) {
        throw "Gen2 full diagnostics are not valid."
    }
    Invoke-ZdtTool -Action "Enable" | Out-Null
    Start-Sleep -Milliseconds 500
    $runBaseline = Invoke-ZdtTool -Action "Status"

    $periodSeconds = 1.0 / $RateHz
    $statusPeriodSeconds = 0.1
    $stageDurationSeconds = $DurationSeconds / [double]$profile.Count
    $statusTimes = @{}
    $capture = [System.IO.MemoryStream]::new()
    $buffer = New-Object byte[] 4096
    $serial = [System.IO.Ports.SerialPort]::new(
        $Port, 230400, [System.IO.Ports.Parity]::None, 8,
        [System.IO.Ports.StopBits]::One)
    $serial.ReadBufferSize = 1MB
    $serial.WriteBufferSize = 1MB
    $serial.Open()
    $serial.DiscardInBuffer()
    Start-Sleep -Milliseconds 100

    $watch = [Diagnostics.Stopwatch]::StartNew()
    $nextMotionIndex = 0
    $nextStatusSeconds = 0.1
    while ($watch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        Read-AvailableBytes $serial $capture $buffer
        $nowSeconds = $watch.Elapsed.TotalSeconds
        $stageIndex = [Math]::Min($profile.Count - 1,
            [int][Math]::Floor($nowSeconds / $stageDurationSeconds))
        $targetSpeedRpm = $profile[$stageIndex]
        $dueIndex = [int][Math]::Floor($nowSeconds / $periodSeconds)
        if ($dueIndex -ge $nextMotionIndex) {
            if ($dueIndex -gt $nextMotionIndex) {
                $hostSkipped += $dueIndex - $nextMotionIndex
            }
            [byte[]]$speedFrame = New-ZdtFrame `
                -CommandSequence (Get-NextSequence) `
                -Action "Speed" -TargetSpeedRpm $targetSpeedRpm `
                -SuppressAck $true
            $serial.Write($speedFrame, 0, $speedFrame.Length)
            $motionSent++
            $nextMotionIndex = $dueIndex + 1
        }
        if ($nowSeconds -ge $nextStatusSeconds) {
            $statusSequence = Get-NextSequence
            $statusTimes[$statusSequence] = [pscustomobject]@{
                TimeSeconds = $nowSeconds
                TargetSpeedRpm = $targetSpeedRpm
                StageIndex = $stageIndex
                StageElapsedSeconds = $nowSeconds -
                    ($stageIndex * $stageDurationSeconds)
            }
            [byte[]]$statusFrame = New-ZdtFrame `
                -CommandSequence $statusSequence -Action "Status"
            $serial.Write($statusFrame, 0, $statusFrame.Length)
            $nextStatusSeconds += $statusPeriodSeconds
        }
        [Threading.Thread]::Sleep(0)
    }
    Start-Sleep -Milliseconds 100
    Read-AvailableBytes $serial $capture $buffer
    $serial.Close()
    $serial.Dispose()
    $serial = $null
    $capturedBytes = $capture.ToArray()
    $capture.Dispose()
    $capture = $null

    $stopSnapshot = Invoke-ZdtTool -Action "Stop"
    Start-Sleep -Milliseconds 700
    $finalSnapshot = Invoke-ZdtTool -Action "Status"

    $samples = @(Get-SpeedSamples $capturedBytes $statusTimes $Axis)
    $steady = @($samples | Where-Object StageElapsedSeconds -ge 0.8)
    if ($steady.Count -lt 50) {
        throw "Too few valid speed samples were captured."
    }
    $speedStats = $steady.SpeedRpm | Measure-Object -Average -Minimum -Maximum
    $firstPosition = $steady[0].PositionMillidegrees
    $lastPosition = $steady[-1].PositionMillidegrees
    $txProperty = "${Axis}TxCommandCount"
    $timeoutProperty = "${Axis}TimeoutCount"
    $invalidProperty = "${Axis}InvalidResponseCount"
    $stalledProperty = "${Axis}Stalled"
    $protectedProperty = "${Axis}StallProtected"
    $motorFrames = [int](
        $stopSnapshot.$txProperty - $runBaseline.$txProperty)
    $actualRate = $motorFrames / [double]$DurationSeconds
    $atTargetCount = @($steady | Where-Object {
        [Math]::Abs($_.SpeedRpm - $_.TargetSpeedRpm) -le 2
    }).Count
    $averageAbsoluteError = ($steady | ForEach-Object {
        [Math]::Abs($_.SpeedRpm - $_.TargetSpeedRpm)
    } | Measure-Object -Average).Average

    $result = [ordered]@{
        Axis = $Axis
        RequestedRateHz = $RateHz
        RequestedSpeedRpm = $SpeedRpm
        SpeedProfileRpm = $profile -join ","
        DurationSeconds = $DurationSeconds
        HostFramesSent = $motionSent
        HostFramesSkipped = $hostSkipped
        MotorSpeedFrames = $motorFrames
        ActualMotorCommandRateHz = [Math]::Round($actualRate, 2)
        ValidRuntimeSamples = $steady.Count
        AverageSpeedRpm = [Math]::Round($speedStats.Average, 2)
        MinimumSpeedRpm = $speedStats.Minimum
        MaximumSpeedRpm = $speedStats.Maximum
        SamplesWithinTwoRpm = $atTargetCount
        AverageAbsoluteErrorRpm = [Math]::Round($averageAbsoluteError, 2)
        PositionTravelDegrees = [Math]::Round(
            ($lastPosition - $firstPosition) / 1000.0, 3)
        TimeoutDelta = $stopSnapshot.$timeoutProperty -
            $runBaseline.$timeoutProperty
        InvalidResponseDelta = $stopSnapshot.$invalidProperty -
            $runBaseline.$invalidProperty
        StalledDuringFinalSample = $stopSnapshot.$stalledProperty
        StallProtectedDuringFinalSample = $stopSnapshot.$protectedProperty
    }
    if ($Axis -eq "Gen2") {
        $voltageStats = $steady.BusVoltageMv |
            Measure-Object -Average -Minimum -Maximum
        $currentStats = $steady.PhaseCurrentMa |
            Measure-Object -Average -Minimum -Maximum
        $result.AverageBusVoltageV = [Math]::Round(
            $voltageStats.Average / 1000.0, 3)
        $result.MinimumBusVoltageV = [Math]::Round(
            $voltageStats.Minimum / 1000.0, 3)
        $result.MaximumBusVoltageV = [Math]::Round(
            $voltageStats.Maximum / 1000.0, 3)
        $result.AveragePhaseCurrentMa = [Math]::Round(
            $currentStats.Average, 1)
        $result.MinimumPhaseCurrentMa = $currentStats.Minimum
        $result.MaximumPhaseCurrentMa = $currentStats.Maximum
    }
    [pscustomobject]$result | Format-List | Out-Host
    if ($profile.Count -gt 1) {
        $stageResults = foreach ($stageIndex in 0..($profile.Count - 1)) {
            $stageSamples = @($steady | Where-Object StageIndex -eq $stageIndex)
            $stageStats = $stageSamples.SpeedRpm |
                Measure-Object -Average -Minimum -Maximum
            $stageError = ($stageSamples | ForEach-Object {
                [Math]::Abs($_.SpeedRpm - $_.TargetSpeedRpm)
            } | Measure-Object -Average).Average
            [pscustomobject]@{
                Stage = $stageIndex + 1
                TargetRpm = $profile[$stageIndex]
                Samples = $stageSamples.Count
                AverageRpm = [Math]::Round($stageStats.Average, 2)
                MinimumRpm = $stageStats.Minimum
                MaximumRpm = $stageStats.Maximum
                AverageAbsoluteErrorRpm = [Math]::Round($stageError, 2)
            }
        }
        Write-Host "Per-stage speed results:"
        $stageResults | Format-Table -AutoSize | Out-Host
    }

    Write-Host "Final complete ZDT snapshot:"
    $finalSnapshot | Format-List | Out-Host
}
finally {
    if ($null -ne $serial) {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        $serial.Dispose()
    }
    if ($null -ne $capture) {
        $capture.Dispose()
    }
    try {
        Invoke-ZdtTool -Action "Stop" | Out-Null
        Invoke-ZdtTool -Action "Disable" | Out-Null
        Start-Sleep -Milliseconds 500
        Invoke-ZdtTool -Action "Deselect" | Out-Null
    } catch {
        Write-Warning "Final stop/disable sequence failed: $_"
    }
}
