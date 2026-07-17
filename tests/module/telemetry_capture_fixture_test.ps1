$ErrorActionPreference = "Stop"

function Get-Crc16Ccitt {
    param([byte[]]$Data, [int]$Offset, [int]$Length)

    [uint32]$crc = 0xFFFF
    for ($index = 0; $index -lt $Length; $index++) {
        $crc = $crc -bxor ([uint32]$Data[$Offset + $index] -shl 8)
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 0x8000) -ne 0) {
                $crc = (($crc -shl 1) -bxor 0x1021) -band 0xFFFF
            }
            else {
                $crc = ($crc -shl 1) -band 0xFFFF
            }
        }
    }
    return [uint16]$crc
}

function New-Frame {
    param(
        [byte]$Type,
        [byte[]]$Payload,
        [uint32]$Sequence,
        [uint32]$TimestampUs
    )

    $frame = New-Object byte[] (16 + $Payload.Length)
    $frame[0] = 0xA5
    $frame[1] = 0x5A
    $frame[2] = 1
    $frame[3] = $Type
    [BitConverter]::GetBytes([uint16]$Payload.Length).CopyTo($frame, 4)
    [BitConverter]::GetBytes($Sequence).CopyTo($frame, 6)
    [BitConverter]::GetBytes($TimestampUs).CopyTo($frame, 10)
    $Payload.CopyTo($frame, 14)
    $crc = Get-Crc16Ccitt -Data $frame -Offset 2 `
        -Length (12 + $Payload.Length)
    [BitConverter]::GetBytes($crc).CopyTo($frame, 14 + $Payload.Length)
    return ,$frame
}

function New-ControlPayload {
    param(
        [uint32]$LoopCount,
        [ValidateSet(40, 44, 96)]
        [int]$PayloadLength = 96
    )

    $payload = New-Object byte[] $payloadLength
    [BitConverter]::GetBytes([single]1.0).CopyTo($payload, 0)
    [BitConverter]::GetBytes([single]0.8).CopyTo($payload, 4)
    [BitConverter]::GetBytes([single]0.2).CopyTo($payload, 8)
    [BitConverter]::GetBytes([single]1.0).CopyTo($payload, 12)
    [BitConverter]::GetBytes($LoopCount).CopyTo($payload, 16)
    [BitConverter]::GetBytes([uint32]10000).CopyTo($payload, 20)
    [BitConverter]::GetBytes([uint32]42).CopyTo($payload, 24)
    [BitConverter]::GetBytes([uint32]1).CopyTo($payload, 28)
    [BitConverter]::GetBytes([uint32]0).CopyTo($payload, 32)
    [BitConverter]::GetBytes([uint32]1).CopyTo($payload, 36)
    if ($PayloadLength -ge 44) {
        [BitConverter]::GetBytes([single]2.0).CopyTo($payload, 40)
    }
    if ($PayloadLength -eq 96) {
        [BitConverter]::GetBytes([single]1.1).CopyTo($payload, 44)
        for ($field = 0; $field -lt 8; $field++) {
            [BitConverter]::GetBytes([single](10 + $field)).CopyTo(
                $payload, 48 + ($field * 4))
        }
        [BitConverter]::GetBytes([single]3.0).CopyTo($payload, 80)
        [BitConverter]::GetBytes([single]8.0).CopyTo($payload, 84)
        [BitConverter]::GetBytes([single]0.0).CopyTo($payload, 88)
        [BitConverter]::GetBytes([uint32]17).CopyTo($payload, 92)
    }
    return ,$payload
}

function New-HealthPayload {
    param([uint32]$SnapshotSequence, [uint32]$UptimeTicks)

    $payload = New-Object byte[] 112
    [BitConverter]::GetBytes([uint16]1).CopyTo($payload, 0)
    [BitConverter]::GetBytes([uint16]0x010F).CopyTo($payload, 2)
    [BitConverter]::GetBytes($SnapshotSequence).CopyTo($payload, 4)
    [BitConverter]::GetBytes($UptimeTicks).CopyTo($payload, 8)
    [BitConverter]::GetBytes([uint32]10000).CopyTo($payload, 20)
    [BitConverter]::GetBytes([uint32]42).CopyTo($payload, 24)
    [BitConverter]::GetBytes([uint32]4096).CopyTo($payload, 56)
    [BitConverter]::GetBytes([uint32]7).CopyTo($payload, 60)
    [BitConverter]::GetBytes([uint16]128).CopyTo($payload, 64)
    $payload[66] = 1
    $payload[70] = 1
    $payload[74] = 4
    $payload[75] = 1
    [BitConverter]::GetBytes([uint32]1600).CopyTo($payload, 76)
    [BitConverter]::GetBytes([uint32]10).CopyTo($payload, 80)
    [BitConverter]::GetBytes([uint32]10).CopyTo($payload, 84)
    [BitConverter]::GetBytes([uint32]38500).CopyTo($payload, 88)
    [BitConverter]::GetBytes([uint32]10).CopyTo($payload, 92)
    [BitConverter]::GetBytes([uint16]180).CopyTo($payload, 96)
    [BitConverter]::GetBytes([uint16]140).CopyTo($payload, 98)
    [BitConverter]::GetBytes([uint16]170).CopyTo($payload, 100)
    [BitConverter]::GetBytes([uint16]100).CopyTo($payload, 102)
    [BitConverter]::GetBytes([uint16]100).CopyTo($payload, 104)
    [BitConverter]::GetBytes([uint16]100).CopyTo($payload, 106)
    [BitConverter]::GetBytes([uint16]372).CopyTo($payload, 108)
    return ,$payload
}

$capturePath = Join-Path $PSScriptRoot "..\..\tools\telemetry_capture.ps1"
$binaryPath = Join-Path ([System.IO.Path]::GetTempPath()) `
    "echo-phase1f-telemetry-fixture.bin"
$jsonPath = Join-Path ([System.IO.Path]::GetTempPath()) `
    "echo-phase1f-telemetry-fixture.json"
$csvPath = Join-Path ([System.IO.Path]::GetTempPath()) `
    "echo-phase1f-telemetry-fixture.csv"
$bytes = [System.Collections.Generic.List[byte]]::new()
[uint32]$sequence = 0

try {
    for ($index = 0; $index -lt 200; $index++) {
        $payloadLength = @(40, 44, 96)[$index % 3]
        $controlPayload = New-ControlPayload $index $payloadLength
        $control = New-Frame -Type 1 -Payload $controlPayload `
            -Sequence $sequence -TimestampUs ([uint32]($index * 10000))
        $bytes.AddRange($control)
        $sequence++
        if (($index -eq 99) -or ($index -eq 199)) {
            $health = New-Frame -Type 4 `
                -Payload (New-HealthPayload ($sequence * 2) ($index * 10)) `
                -Sequence $sequence `
                -TimestampUs ([uint32](($index * 10000) + 9999))
            $bytes.AddRange($health)
            $sequence++
        }
    }
    [System.IO.File]::WriteAllBytes($binaryPath, $bytes.ToArray())
    & $capturePath -InputPath $binaryPath -JsonPath $jsonPath `
        -CsvPath $csvPath | Out-Host
    if (($null -ne $LASTEXITCODE) -and ($LASTEXITCODE -ne 0)) {
        throw "telemetry_capture.ps1 returned $LASTEXITCODE"
    }

    $summary = Get-Content -Raw -LiteralPath $jsonPath | ConvertFrom-Json
    $csvRows = @(Import-Csv -LiteralPath $csvPath)
    $extendedRow = $csvRows | Where-Object {
        $_.parameter_apply_sequence -eq "17"
    } | Select-Object -First 1
    if (($summary.ValidFrames -ne 202) -or
        ($summary.ControlFrames -ne 200) -or
        ($summary.HealthFrames -ne 2) -or
        ($summary.CrcErrors -ne 0) -or
        ($summary.SequenceGaps -ne 0) -or
        ($summary.ControlRateHz -ne 100.0) -or
        ($summary.HealthRateHz -ne 1.0) -or
        ($summary.LatestHealth.BuildPhase -ne "0x010F") -or
        ($summary.LatestHealth.ParameterApplySequence -ne 7) -or
        ($summary.LatestHealth.MinimumStackFreeWords -ne 128) -or
        ($summary.LatestHealth.ResetReason -ne 4) -or
        ($summary.LatestHealth.ResetReasonValid -ne 1) -or
        ($summary.LatestHealth.I2cSuccessCount -ne 1600) -or
        ($summary.LatestHealth.QuietAcquiredCount -ne 10) -or
        ($summary.LatestHealth.QuietReleasedCount -ne 10) -or
        ($summary.LatestHealth.DisplayRefreshCount -ne 10) -or
        ($summary.LatestHealth.ServiceStackFreeWords -ne 140) -or
        ($summary.LatestHealth.SerialRingHighWaterBytes -ne 372) -or
        ($csvRows.Count -ne 200) -or ($null -eq $extendedRow) -or
        ([Math]::Abs([double]$extendedRow.right_setpoint - 1.1) -gt 0.0001) -or
        ([Math]::Abs([double]$extendedRow.left_pid_proportional - 10.0) -gt 0.0001) -or
        ([Math]::Abs([double]$extendedRow.right_pid_feedforward - 17.0) -gt 0.0001) -or
        ([Math]::Abs([double]$extendedRow.active_kp - 3.0) -gt 0.0001) -or
        ([Math]::Abs([double]$extendedRow.active_ki - 8.0) -gt 0.0001)) {
        throw "Telemetry fixture summary did not match expected values."
    }
    Write-Output "telemetry capture fixture: PASS"
}
finally {
    Remove-Item -LiteralPath $binaryPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $jsonPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $csvPath -Force -ErrorAction SilentlyContinue
}
