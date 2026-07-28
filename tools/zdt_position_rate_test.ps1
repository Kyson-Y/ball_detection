[CmdletBinding()]
param(
    [string]$Port = "COM4",
    [ValidateSet("Gen1", "Gen2")]
    [string]$Axis = "Gen2",
    [switch]$AllowGen1PositionInterrupt,
    [switch]$ContinueAfterUnstable,
    [int[]]$RatesHz = @(100, 200, 300, 400),
    [ValidateRange(1.0, 30.0)]
    [double]$DurationSeconds = 2.0,
    [ValidateRange(1000, 15000)]
    [int]$AmplitudeMillidegrees = 5000,
    [ValidateRange(0.25, 2.0)]
    [double]$WaveformHz = 1.0,
    [ValidateSet("Sine", "Triangle")]
    [string]$Waveform = "Triangle",
    [ValidateRange(10, 180)]
    [int]$PositionSpeedRpm = 60,
    [ValidateRange(0, 3000)]
    [int]$AccelerationRpmPerSecond = 1000,
    [switch]$ConfirmUserPresent,
    [switch]$ConfirmMechanismSuspended,
    [switch]$ConfirmCurrentLimitedSupply,
    [switch]$ConfirmPhysicalDisconnectReady
)

$ErrorActionPreference = "Stop"

if (-not $ConfirmUserPresent -or
    -not $ConfirmMechanismSuspended -or
    -not $ConfirmCurrentLimitedSupply -or
    -not $ConfirmPhysicalDisconnectReady) {
    throw "Rate testing requires all four physical safety confirmations."
}
foreach ($rate in $RatesHz) {
    if ($rate -lt 10 -or $rate -gt 400) {
        throw "Each requested rate must be between 10 and 400 Hz."
    }
}
if ($AllowGen1PositionInterrupt -and $Axis -ne "Gen1") {
    throw "AllowGen1PositionInterrupt is only valid with Axis Gen1."
}

$commandTool = Join-Path $PSScriptRoot "zdt_backup_command.ps1"
$sequence = [uint32](Get-Random -Minimum 1000 -Maximum 2000000000)
$results = [System.Collections.Generic.List[object]]::new()

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

function Get-TargetPosition {
    param([int]$Center, [double]$TimeSeconds)

    $phase = 2.0 * [Math]::PI * $WaveformHz * $TimeSeconds
    $normalized = if ($Waveform -eq "Triangle") {
        (2.0 / [Math]::PI) * [Math]::Asin([Math]::Sin($phase))
    } else {
        [Math]::Sin($phase)
    }
    return $Center + [int][Math]::Round(
        $AmplitudeMillidegrees * $normalized)
}

function New-ZdtCommandFrame {
    param(
        [uint32]$CommandSequence,
        [ValidateSet("Status", "Position")]
        [string]$Action,
        [int]$Value = 0,
        [bool]$SuppressAck = $false
    )

    $operation = if ($Action -eq "Position") { 5 } else { 0 }
    $frame = New-Object byte[] 44
    $frame[0] = 0xA5
    $frame[1] = 0x5A
    $frame[2] = 1
    $frame[3] = 14
    [BitConverter]::GetBytes([uint16]28).CopyTo($frame, 4)
    [BitConverter]::GetBytes($CommandSequence).CopyTo($frame, 6)
    [BitConverter]::GetBytes([uint32]0).CopyTo($frame, 10)
    $magic = [Convert]::ToUInt32("5A445442", 16)
    $magicInverse = [Convert]::ToUInt32("A5BBABBD", 16)
    [BitConverter]::GetBytes($magic).CopyTo($frame, 14)
    [BitConverter]::GetBytes($magicInverse).CopyTo($frame, 18)
    [BitConverter]::GetBytes($CommandSequence).CopyTo($frame, 22)
    $frame[26] = [byte]$operation
    $frame[27] = if ($Axis -eq "Gen1") { 0 } else { 1 }
    $frame[28] = 1
    $commandFlags = if ($SuppressAck) { 1 } else { 0 }
    if ($AllowGen1PositionInterrupt -and $Action -eq "Position") {
        $commandFlags = $commandFlags -bor 2
    }
    $frame[29] = [byte]$commandFlags
    [BitConverter]::GetBytes([int32]$Value).CopyTo($frame, 30)
    [BitConverter]::GetBytes([uint16]$PositionSpeedRpm).CopyTo($frame, 34)
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

function Get-TrackingSamples {
    param(
        [byte[]]$Bytes,
        [hashtable]$StatusTargets,
        [string]$SelectedAxis
    )

    $samples = [System.Collections.Generic.List[object]]::new()
    for ($index = 0; $index + 120 -le $Bytes.Length; $index++) {
        $payloadLength = Get-U16 $Bytes ($index + 4)
        $frameLength = 16 + $payloadLength
        if ($Bytes[$index] -ne 0xA5 -or
            $Bytes[$index + 1] -ne 0x5A -or
            $Bytes[$index + 2] -ne 1 -or
            $Bytes[$index + 3] -ne 15 -or
            $payloadLength -notin @(104, 148) -or
            $index + $frameLength -gt $Bytes.Length) {
            continue
        }
        $expectedCrc = Get-U16 $Bytes ($index + 14 + $payloadLength)
        $actualCrc = Get-Crc16Ccitt -Data $Bytes `
            -Offset ($index + 2) -Length (12 + $payloadLength)
        if ($expectedCrc -ne $actualCrc) {
            continue
        }
        $ackSequence = Get-U32 $Bytes ($index + 14)
        if (-not $StatusTargets.ContainsKey($ackSequence)) {
            continue
        }
        $target = $StatusTargets[$ackSequence]
        $axisOffset = if ($SelectedAxis -eq "Gen1") { 38 } else { 78 }
        $position = Get-I32 $Bytes ($index + $axisOffset + 24)
        $samples.Add([pscustomobject]@{
            TimeSeconds = $target.TimeSeconds
            TargetMillidegrees = $target.TargetMillidegrees
            PositionMillidegrees = $position
            ErrorMillidegrees = $position - $target.TargetMillidegrees
            SpeedRpm = Get-I16 $Bytes ($index + $axisOffset + 28)
            MotorStatusFlags = $Bytes[$index + $axisOffset + 34]
            Tracking = $target.Tracking
        })
    }
    return $samples
}

function Invoke-RateStage {
    param([int]$RateHz)

    $baseline = Invoke-ZdtTool -Action "Status"
    $onlineProperty = "${Axis}Online"
    $enabledProperty = "${Axis}Enabled"
    $positionProperty = "${Axis}PositionMillidegrees"
    $txProperty = "${Axis}TxCommandCount"
    $responseProperty = "${Axis}ResponseCount"
    $timeoutProperty = "${Axis}TimeoutCount"
    $invalidProperty = "${Axis}InvalidResponseCount"
    $stalledProperty = "${Axis}Stalled"
    $protectedProperty = "${Axis}StallProtected"
    if (-not $baseline.$onlineProperty -or
        -not $baseline.$enabledProperty) {
        throw "$Axis must be online and enabled before rate testing."
    }
    $center = [int]$baseline.$positionProperty
    $periodSeconds = 1.0 / $RateHz
    $statusPeriodSeconds = 0.05
    $statusTargets = @{}
    $capture = [System.IO.MemoryStream]::new()
    $buffer = New-Object byte[] 4096
    $serial = [System.IO.Ports.SerialPort]::new(
        $Port, 230400, [System.IO.Ports.Parity]::None, 8,
        [System.IO.Ports.StopBits]::One)
    $serial.ReadBufferSize = 1MB
    $serial.WriteBufferSize = 1MB
    $motionSent = 0
    $skipped = 0
    $nextMotionIndex = 0
    $nextStatusSeconds = 0.05

    try {
        $serial.Open()
        $serial.DiscardInBuffer()
        Start-Sleep -Milliseconds 100
        $watch = [Diagnostics.Stopwatch]::StartNew()

        while ($watch.Elapsed.TotalSeconds -lt $DurationSeconds) {
            Read-AvailableBytes $serial $capture $buffer
            $nowSeconds = $watch.Elapsed.TotalSeconds
            $dueIndex = [int][Math]::Floor($nowSeconds / $periodSeconds)
            if ($dueIndex -ge $nextMotionIndex) {
                if ($dueIndex -gt $nextMotionIndex) {
                    $skipped += $dueIndex - $nextMotionIndex
                }
                $targetTime = $dueIndex * $periodSeconds
                $target = Get-TargetPosition $center $targetTime
                [byte[]]$frame = New-ZdtCommandFrame `
                    -CommandSequence (Get-NextSequence) `
                    -Action "Position" -Value $target -SuppressAck $true
                $serial.Write($frame, 0, $frame.Length)
                $motionSent++
                $nextMotionIndex = $dueIndex + 1
            }

            if ($nowSeconds -ge $nextStatusSeconds) {
                $statusSequence = Get-NextSequence
                $target = Get-TargetPosition $center $nowSeconds
                $statusTargets[$statusSequence] = [pscustomobject]@{
                    TimeSeconds = $nowSeconds
                    TargetMillidegrees = $target
                    Tracking = $true
                }
                [byte[]]$statusFrame = New-ZdtCommandFrame `
                    -CommandSequence $statusSequence -Action "Status"
                $serial.Write($statusFrame, 0, $statusFrame.Length)
                $nextStatusSeconds += $statusPeriodSeconds
            }
            [Threading.Thread]::Sleep(0)
        }

        [byte[]]$centerFrame = New-ZdtCommandFrame `
            -CommandSequence (Get-NextSequence) `
            -Action "Position" -Value $center -SuppressAck $true
        $serial.Write($centerFrame, 0, $centerFrame.Length)

        $settleEnd = $watch.Elapsed.TotalSeconds + 1.5
        $nextSettleStatus = $watch.Elapsed.TotalSeconds
        while ($watch.Elapsed.TotalSeconds -lt $settleEnd) {
            Read-AvailableBytes $serial $capture $buffer
            $nowSeconds = $watch.Elapsed.TotalSeconds
            if ($nowSeconds -ge $nextSettleStatus) {
                $statusSequence = Get-NextSequence
                $statusTargets[$statusSequence] = [pscustomobject]@{
                    TimeSeconds = $nowSeconds
                    TargetMillidegrees = $center
                    Tracking = $false
                }
                [byte[]]$statusFrame = New-ZdtCommandFrame `
                    -CommandSequence $statusSequence -Action "Status"
                $serial.Write($statusFrame, 0, $statusFrame.Length)
                $nextSettleStatus += $statusPeriodSeconds
            }
            [Threading.Thread]::Sleep(0)
        }
        Start-Sleep -Milliseconds 100
        Read-AvailableBytes $serial $capture $buffer
    }
    finally {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        $serial.Dispose()
    }

    [byte[]]$capturedBytes = $capture.ToArray()
    $capture.Dispose()
    $samples = @(Get-TrackingSamples $capturedBytes $statusTargets $Axis)
    $trackingSamples = @($samples | Where-Object Tracking)
    $final = Invoke-ZdtTool -Action "Status"
    $errors = @($trackingSamples | ForEach-Object ErrorMillidegrees)
    $rmsError = if ($errors.Count -gt 0) {
        [Math]::Sqrt((($errors | ForEach-Object { $_ * $_ } |
            Measure-Object -Average).Average))
    } else {
        [double]::PositiveInfinity
    }
    $maxError = if ($errors.Count -gt 0) {
        ($errors | ForEach-Object { [Math]::Abs($_) } |
            Measure-Object -Maximum).Maximum
    } else {
        [double]::PositiveInfinity
    }
    $transmitted = [int](
        $final.$txProperty - $baseline.$txProperty - 1)
    $actualRate = $transmitted / $DurationSeconds
    $finalError = [Math]::Abs(
        $final.$positionProperty - $center)
    $transportStable = $final.$onlineProperty -and
        (-not $final.$stalledProperty) -and
        (-not $final.$protectedProperty) -and
        ($final.$invalidProperty -eq $baseline.$invalidProperty) -and
        ($final.$timeoutProperty -eq $baseline.$timeoutProperty) -and
        ($actualRate -ge (0.9 * $RateHz)) -and
        ($trackingSamples.Count -ge 10) -and
        ($finalError -le 1000)

    $result = [pscustomobject]@{
        Axis = $Axis
        PositionInterruptEnabled = [bool]$AllowGen1PositionInterrupt
        RequestedHz = $RateHz
        HostSent = $motionSent
        HostSkipped = $skipped
        MotorTransmitted = $transmitted
        ActualMotorHz = [Math]::Round($actualRate, 1)
        TrackingSamples = $trackingSamples.Count
        RmsErrorDegrees = [Math]::Round($rmsError / 1000.0, 3)
        MaxErrorDegrees = [Math]::Round($maxError / 1000.0, 3)
        FinalErrorDegrees = [Math]::Round($finalError / 1000.0, 3)
        ResponseDelta = $final.$responseProperty - $baseline.$responseProperty
        TimeoutDelta = $final.$timeoutProperty - $baseline.$timeoutProperty
        InvalidDelta = $final.$invalidProperty - $baseline.$invalidProperty
        Stalled = $final.$stalledProperty
        TransportStable = $transportStable
    }
    $results.Add($result)
    $result | Format-List | Out-Host
    return $result
}

try {
    Invoke-ZdtTool -Action "Select" | Out-Null
    Start-Sleep -Milliseconds 500
    $online = Invoke-ZdtTool -Action "Status"
    $onlineProperty = "${Axis}Online"
    if (-not $online.$onlineProperty) {
        throw "$Axis is offline."
    }
    Invoke-ZdtTool -Action "Enable" | Out-Null
    Start-Sleep -Milliseconds 500

    foreach ($rate in $RatesHz) {
        $result = Invoke-RateStage -RateHz $rate
        if (-not $result.TransportStable -and -not $ContinueAfterUnstable) {
            break
        }
        Start-Sleep -Milliseconds 500
    }
}
finally {
    try {
        Invoke-ZdtTool -Action "Stop" | Out-Null
        Invoke-ZdtTool -Action "Disable" | Out-Null
        Start-Sleep -Milliseconds 500
        Invoke-ZdtTool -Action "Deselect" | Out-Null
    } catch {
        Write-Warning "Final stop/disable sequence failed: $_"
    }
}

$results | Format-Table -AutoSize
$stableRates = @($results | Where-Object TransportStable |
    Select-Object -ExpandProperty RequestedHz)
if ($stableRates.Count -gt 0) {
    Write-Host "Highest transport-stable tested rate: $($stableRates[-1]) Hz"
    $bestTracking = $results | Where-Object TransportStable |
        Sort-Object RmsErrorDegrees | Select-Object -First 1
    Write-Host ("Lowest RMS tracking error: {0} Hz ({1} deg)" -f `
        $bestTracking.RequestedHz, $bestTracking.RmsErrorDegrees)
} else {
    if ($ContinueAfterUnstable) {
        Write-Warning "No requested rate passed the strict transport criteria."
    } else {
        throw "No requested position update rate passed the transport criteria."
    }
}
