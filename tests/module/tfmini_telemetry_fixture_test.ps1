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

function New-TfminiPayload {
    param([uint32]$SampleSequence, [uint16]$DistanceCm)

    $payload = New-Object byte[] 64
    [BitConverter]::GetBytes($SampleSequence).CopyTo($payload, 0)
    [BitConverter]::GetBytes([uint32]500000).CopyTo($payload, 4)
    [BitConverter]::GetBytes([uint32]120).CopyTo($payload, 8)
    [BitConverter]::GetBytes($DistanceCm).CopyTo($payload, 12)
    [BitConverter]::GetBytes([uint16]2334).CopyTo($payload, 14)
    [BitConverter]::GetBytes([int16]5600).CopyTo($payload, 16)
    $payload[18] = 1
    $payload[19] = 0x07
    [BitConverter]::GetBytes([uint32]10000).CopyTo($payload, 20)
    [BitConverter]::GetBytes([uint32]100000).CopyTo($payload, 24)
    [BitConverter]::GetBytes($SampleSequence).CopyTo($payload, 28)
    [BitConverter]::GetBytes($SampleSequence).CopyTo($payload, 32)
    [BitConverter]::GetBytes([uint32]1).CopyTo($payload, 44)
    $payload[60] = 0x07
    $payload[61] = 0x03
    $payload[62] = 0x02
    $payload[63] = 1
    return ,$payload
}

function New-Frame {
    param(
        [byte[]]$Payload,
        [uint32]$Sequence,
        [uint32]$TimestampUs
    )

    $frame = New-Object byte[] (16 + $Payload.Length)
    $frame[0] = 0xA5
    $frame[1] = 0x5A
    $frame[2] = 1
    $frame[3] = 10
    [BitConverter]::GetBytes([uint16]$Payload.Length).CopyTo($frame, 4)
    [BitConverter]::GetBytes($Sequence).CopyTo($frame, 6)
    [BitConverter]::GetBytes($TimestampUs).CopyTo($frame, 10)
    $Payload.CopyTo($frame, 14)
    $crc = Get-Crc16Ccitt -Data $frame -Offset 2 `
        -Length (12 + $Payload.Length)
    [BitConverter]::GetBytes($crc).CopyTo($frame, 14 + $Payload.Length)
    return ,$frame
}

$capturePath = Join-Path $PSScriptRoot "..\..\tools\telemetry_capture.ps1"
$binaryPath = Join-Path ([System.IO.Path]::GetTempPath()) `
    "echo-tfmini-telemetry-fixture.bin"
$jsonPath = Join-Path ([System.IO.Path]::GetTempPath()) `
    "echo-tfmini-telemetry-fixture.json"
$bytes = [System.Collections.Generic.List[byte]]::new()

try {
    $bytes.AddRange((New-Frame -Payload (New-TfminiPayload 100 104) `
        -Sequence 0 -TimestampUs 0))
    $bytes.AddRange((New-Frame -Payload (New-TfminiPayload 105 103) `
        -Sequence 1 -TimestampUs 50000))
    [System.IO.File]::WriteAllBytes($binaryPath, $bytes.ToArray())

    & $capturePath -InputPath $binaryPath -JsonPath $jsonPath | Out-Host
    if (($null -ne $LASTEXITCODE) -and ($LASTEXITCODE -ne 0)) {
        throw "telemetry_capture.ps1 returned $LASTEXITCODE"
    }

    $summary = Get-Content -Raw -LiteralPath $jsonPath | ConvertFrom-Json
    if (($summary.ValidFrames -ne 2) -or
        ($summary.TfminiFrames -ne 2) -or
        ($summary.TfminiTelemetryRateHz -ne 20.0) -or
        ($summary.TfminiDeviceFrameRateHz -ne 100.0) -or
        ($summary.TfminiDistanceMinimumCm -ne 103) -or
        ($summary.TfminiDistanceMaximumCm -ne 104) -or
        ($summary.TfminiDistanceAverageCm -ne 103.5) -or
        ($summary.LatestTfmini.DistanceCm -ne 103) -or
        ($summary.LatestTfmini.Strength -ne 2334) -or
        ($summary.LatestTfmini.TemperatureC -ne 56.0) -or
        ($summary.LatestTfmini.FirmwareVersion -ne "2.3.7") -or
        ($summary.LatestTfmini.ChecksumErrorCount -ne 0) -or
        ($summary.LatestTfmini.UartRxOverflowCount -ne 0)) {
        throw "TFmini telemetry fixture summary did not match expected values."
    }
    Write-Output "tfmini telemetry fixture: PASS"
}
finally {
    Remove-Item -LiteralPath $binaryPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $jsonPath -Force -ErrorAction SilentlyContinue
}
