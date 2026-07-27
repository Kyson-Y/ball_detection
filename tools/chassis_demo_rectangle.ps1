[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [ValidateRange(80, 500)]
    [int]$InterCommandGapMs = 120,
    [string]$OutputDirectory = "",
    [switch]$ConfirmUserPresent,
    [switch]$ConfirmGroundClear,
    [switch]$ConfirmBothMotorsConnected,
    [switch]$ConfirmBatteryPowered,
    [switch]$ConfirmPhysicalDisconnectReady
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

if (-not $ConfirmUserPresent -or -not $ConfirmGroundClear -or
    -not $ConfirmBothMotorsConnected -or -not $ConfirmBatteryPowered -or
    -not $ConfirmPhysicalDisconnectReady) {
    throw "The demonstration requires all physical safety confirmations."
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $EchoPaths.ProjectRoot `
        "tests\artifacts\rectangle-demo-$timestamp"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$rawPath = Join-Path $OutputDirectory "capture.bin"
$eventPath = Join-Path $OutputDirectory "events.json"
$summaryPath = Join-Path $OutputDirectory "capture.json"
$csvPath = Join-Path $OutputDirectory "control.csv"
$attitudePath = Join-Path $OutputDirectory "attitude.csv"

$ModeSpeed = 1
$ModeHeading = 2
$ModeDistance = 3
$FlagClosedLoop = [uint32]32
$script:Rx = [Collections.Generic.List[byte]]::new()
$script:Capture = [IO.MemoryStream]::new(2MB)

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

function New-ActuatorCommand {
    param(
        [uint32]$Sequence,
        [int16]$LeftField,
        [int16]$RightField,
        [uint16]$DurationMs,
        [uint16]$Mode
    )

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
    [BitConverter]::GetBytes($Sequence).CopyTo($command, 22)
    [BitConverter]::GetBytes($LeftField).CopyTo($command, 26)
    [BitConverter]::GetBytes($RightField).CopyTo($command, 28)
    [BitConverter]::GetBytes($DurationMs).CopyTo($command, 30)
    [BitConverter]::GetBytes($Mode).CopyTo($command, 32)
    $crc = Get-Crc16Ccitt -Data $command -Offset 2 -Length 32
    [BitConverter]::GetBytes($crc).CopyTo($command, 34)
    return $command
}

function Receive-Frames {
    param([IO.Ports.SerialPort]$Serial)

    $frames = [Collections.Generic.List[byte[]]]::new()
    $buffer = New-Object byte[] 4096
    while ($Serial.BytesToRead -gt 0) {
        $count = [Math]::Min($Serial.BytesToRead, $buffer.Length)
        $read = $Serial.Read($buffer, 0, $count)
        if ($read -le 0) {
            break
        }
        $script:Capture.Write($buffer, 0, $read)
        for ($index = 0; $index -lt $read; $index++) {
            $script:Rx.Add($buffer[$index])
        }
    }

    while ($script:Rx.Count -ge 6) {
        if ($script:Rx[0] -ne 0xA5 -or $script:Rx[1] -ne 0x5A) {
            $script:Rx.RemoveAt(0)
            continue
        }
        $payloadLength = [int]$script:Rx[4] -bor
            ([int]$script:Rx[5] -shl 8)
        if ($payloadLength -lt 0 -or $payloadLength -gt 128) {
            $script:Rx.RemoveAt(0)
            continue
        }
        $frameLength = 16 + $payloadLength
        if ($script:Rx.Count -lt $frameLength) {
            break
        }
        $frame = $script:Rx.GetRange(0, $frameLength).ToArray()
        $script:Rx.RemoveRange(0, $frameLength)
        $expected = [BitConverter]::ToUInt16($frame, 14 + $payloadLength)
        $actual = Get-Crc16Ccitt -Data $frame -Offset 2 `
            -Length (12 + $payloadLength)
        if ($expected -eq $actual) {
            $frames.Add($frame)
        }
    }
    return $frames.ToArray()
}

function Send-SafeStop {
    param([IO.Ports.SerialPort]$Serial)

    if (-not $Serial.IsOpen) {
        return
    }
    $sequence = [uint32](Get-Random -Minimum 1 -Maximum 2000000000)
    $command = New-ActuatorCommand -Sequence $sequence -LeftField 0 `
        -RightField 0 -DurationMs 0 -Mode $ModeSpeed
    $Serial.Write($command, 0, $command.Length)
    Start-Sleep -Milliseconds 100
}

function Invoke-Motion {
    param(
        [IO.Ports.SerialPort]$Serial,
        [pscustomobject]$Motion,
        [uint32]$Sequence
    )

    $command = New-ActuatorCommand -Sequence $Sequence `
        -LeftField ([int16]$Motion.LeftField) `
        -RightField ([int16]$Motion.RightField) `
        -DurationMs ([uint16]$Motion.DurationMs) `
        -Mode ([uint16]$Motion.Mode)
    $Serial.Write($command, 0, $command.Length)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $activeSeen = $false
    $ackSeen = $false
    $inactiveFrames = 0
    $startTimestampUs = [uint32]0
    $stopTimestampUs = [uint32]0

    while ($watch.Elapsed.TotalSeconds -lt $Motion.HostTimeoutSeconds) {
        $frames = @(Receive-Frames -Serial $Serial)
        foreach ($frame in $frames) {
            $frameType = $frame[3]
            $payloadLength = [BitConverter]::ToUInt16($frame, 4)
            $timestampUs = [BitConverter]::ToUInt32($frame, 10)
            $payloadOffset = 14
            if ($frameType -eq 6 -and $payloadLength -eq 16) {
                $ackSequence = [BitConverter]::ToUInt32(
                    $frame, $payloadOffset)
                if ($ackSequence -eq $Sequence) {
                    if ($frame[$payloadOffset + 10] -ne 0) {
                        throw "$($Motion.Name) was rejected with status $($frame[$payloadOffset + 10])."
                    }
                    $ackSeen = $true
                }
            } elseif ($frameType -eq 1 -and $payloadLength -eq 96) {
                $flags = [BitConverter]::ToUInt32(
                    $frame, $payloadOffset + 36)
                if (($flags -band $FlagClosedLoop) -ne 0) {
                    if (-not $activeSeen) {
                        $startTimestampUs = $timestampUs
                    }
                    $activeSeen = $true
                    $inactiveFrames = 0
                } elseif ($activeSeen) {
                    $inactiveFrames++
                    if ($inactiveFrames -ge 5) {
                        $stopTimestampUs = $timestampUs
                        return [pscustomobject]@{
                            Name = $Motion.Name
                            Sequence = $Sequence
                            AckSeen = $ackSeen
                            StartTimestampUs = $startTimestampUs
                            StopTimestampUs = $stopTimestampUs
                            ActiveSeconds = [Math]::Round(
                                (($stopTimestampUs - $startTimestampUs) /
                                    1000000.0), 3)
                            HostElapsedSeconds = [Math]::Round(
                                $watch.Elapsed.TotalSeconds, 3)
                        }
                    }
                }
            } elseif ($frameType -eq 4 -and $payloadLength -ge 112) {
                $activeIssues = [BitConverter]::ToUInt32(
                    $frame, $payloadOffset + 12)
                if ($activeIssues -ne 0) {
                    throw ("$($Motion.Name) health fault 0x{0:X8}." -f
                        $activeIssues)
                }
            }
        }
        Start-Sleep -Milliseconds 2
    }
    throw "$($Motion.Name) did not complete before its host timeout."
}

$motions = @(
    [pscustomobject]@{ Name = "pivot-right-360"; Mode = $ModeHeading;
        LeftField = 0; RightField = -3600; DurationMs = 12000;
        HostTimeoutSeconds = 15 },
    [pscustomobject]@{ Name = "straight-1600-a"; Mode = $ModeDistance;
        LeftField = 600; RightField = 1600; DurationMs = 16000;
        HostTimeoutSeconds = 19 },
    [pscustomobject]@{ Name = "pivot-right-90-a"; Mode = $ModeHeading;
        LeftField = 0; RightField = -900; DurationMs = 5000;
        HostTimeoutSeconds = 8 },
    [pscustomobject]@{ Name = "straight-500-a"; Mode = $ModeDistance;
        LeftField = 400; RightField = 500; DurationMs = 12000;
        HostTimeoutSeconds = 15 },
    [pscustomobject]@{ Name = "pivot-right-90-b"; Mode = $ModeHeading;
        LeftField = 0; RightField = -900; DurationMs = 5000;
        HostTimeoutSeconds = 8 },
    [pscustomobject]@{ Name = "straight-1600-b"; Mode = $ModeDistance;
        LeftField = 600; RightField = 1600; DurationMs = 16000;
        HostTimeoutSeconds = 19 },
    [pscustomobject]@{ Name = "pivot-right-90-c"; Mode = $ModeHeading;
        LeftField = 0; RightField = -900; DurationMs = 5000;
        HostTimeoutSeconds = 8 },
    [pscustomobject]@{ Name = "straight-500-b"; Mode = $ModeDistance;
        LeftField = 400; RightField = 500; DurationMs = 12000;
        HostTimeoutSeconds = 15 },
    [pscustomobject]@{ Name = "pivot-left-360"; Mode = $ModeHeading;
        LeftField = 0; RightField = 3600; DurationMs = 12000;
        HostTimeoutSeconds = 15 },
    [pscustomobject]@{ Name = "pivot-right-90-final"; Mode = $ModeHeading;
        LeftField = 0; RightField = -900; DurationMs = 5000;
        HostTimeoutSeconds = 8 },
    [pscustomobject]@{ Name = "straight-500-c"; Mode = $ModeDistance;
        LeftField = 400; RightField = 500; DurationMs = 12000;
        HostTimeoutSeconds = 15 },
    [pscustomobject]@{ Name = "reverse-500"; Mode = $ModeDistance;
        LeftField = -400; RightField = 500; DurationMs = 12000;
        HostTimeoutSeconds = 15 }
)

$serial = [IO.Ports.SerialPort]::new(
    $Port, $BaudRate, [IO.Ports.Parity]::None, 8,
    [IO.Ports.StopBits]::One)
$serial.ReadBufferSize = 1MB
$serial.WriteBufferSize = 4096
$serial.ReadTimeout = 20
$events = [Collections.Generic.List[object]]::new()
$completed = $false
try {
    $serial.Open()
    $serial.DiscardInBuffer()
    Start-Sleep -Milliseconds 100
    foreach ($motion in $motions) {
        $sequence = [uint32](Get-Random -Minimum 1 -Maximum 2000000000)
        Write-Host ("Starting {0}" -f $motion.Name)
        $event = Invoke-Motion -Serial $serial -Motion $motion `
            -Sequence $sequence
        $events.Add($event)
        Write-Host ("Completed {0} in {1:F3} s" -f
            $motion.Name, $event.ActiveSeconds)
        Start-Sleep -Milliseconds $InterCommandGapMs
    }
    $completed = $true
} finally {
    if ($serial.IsOpen) {
        Send-SafeStop -Serial $serial
    }
    if ($serial.IsOpen) {
        Start-Sleep -Milliseconds 200
        [void](Receive-Frames -Serial $serial)
        $serial.Close()
    }
    $serial.Dispose()
    [IO.File]::WriteAllBytes($rawPath, $script:Capture.ToArray())
    $script:Capture.Dispose()
    $events | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath $eventPath -Encoding UTF8
}

$captureTool = Join-Path $PSScriptRoot "telemetry_capture.ps1"
& $captureTool -InputPath $rawPath -CsvPath $csvPath `
    -AttitudeCsvPath $attitudePath -JsonPath $summaryPath | Out-Host
$parserExitCode = $LASTEXITCODE
[pscustomobject]@{
    Result = "completed"
    MotionCount = $events.Count
    InterCommandGapMs = $InterCommandGapMs
    ParserExitCode = $parserExitCode
    OutputDirectory = $OutputDirectory
} | Format-List
