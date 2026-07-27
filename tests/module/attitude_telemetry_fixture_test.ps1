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

function New-AttitudePayload {
    param([uint32]$ImuSampleCount)

    $payload = New-Object byte[] 64
    [BitConverter]::GetBytes([uint16]1).CopyTo($payload, 0)
    $payload[2] = 0x07
    [BitConverter]::GetBytes($ImuSampleCount).CopyTo($payload, 4)
    [BitConverter]::GetBytes([uint32]500000).CopyTo($payload, 8)
    [BitConverter]::GetBytes([uint32]2468).CopyTo($payload, 12)
    [BitConverter]::GetBytes([single]1.25).CopyTo($payload, 16)
    [BitConverter]::GetBytes([single]-2.5).CopyTo($payload, 20)
    [BitConverter]::GetBytes([single]45.0).CopyTo($payload, 24)
    [BitConverter]::GetBytes([single]0.1).CopyTo($payload, 28)
    [BitConverter]::GetBytes([single]-0.2).CopyTo($payload, 32)
    [BitConverter]::GetBytes([single]3.0).CopyTo($payload, 36)
    [BitConverter]::GetBytes([single]1.004).CopyTo($payload, 40)
    [BitConverter]::GetBytes([single]1.0).CopyTo($payload, 44)
    [BitConverter]::GetBytes([single]0.01).CopyTo($payload, 48)
    [BitConverter]::GetBytes([uint32]1200).CopyTo($payload, 52)
    [BitConverter]::GetBytes([uint32]3).CopyTo($payload, 56)
    [BitConverter]::GetBytes([uint32]0).CopyTo($payload, 60)
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
    $frame[3] = 13
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
    "echo-attitude-telemetry-fixture.bin"
$jsonPath = Join-Path ([System.IO.Path]::GetTempPath()) `
    "echo-attitude-telemetry-fixture.json"
$csvPath = Join-Path ([System.IO.Path]::GetTempPath()) `
    "echo-attitude-telemetry-fixture.csv"
$bytes = [System.Collections.Generic.List[byte]]::new()

try {
    $bytes.AddRange((New-Frame -Payload (New-AttitudePayload 1000) `
        -Sequence 0 -TimestampUs 500070))
    $bytes.AddRange((New-Frame -Payload (New-AttitudePayload 1005) `
        -Sequence 1 -TimestampUs 550070))
    [System.IO.File]::WriteAllBytes($binaryPath, $bytes.ToArray())

    & $capturePath -InputPath $binaryPath -AttitudeCsvPath $csvPath `
        -JsonPath $jsonPath | Out-Host
    if (($null -ne $LASTEXITCODE) -and ($LASTEXITCODE -ne 0)) {
        throw "telemetry_capture.ps1 returned $LASTEXITCODE"
    }

    $summary = Get-Content -Raw -LiteralPath $jsonPath | ConvertFrom-Json
    if (($summary.ValidFrames -ne 2) -or
        ($summary.AttitudeFrames -ne 2) -or
        ($summary.AttitudeTelemetryRateHz -ne 20.0) -or
        ($summary.AttitudeSampleRateHz -ne 100.0) -or
        ($summary.LatestAttitude.SchemaVersion -ne 1) -or
        ($summary.LatestAttitude.RollDeg -ne 1.25) -or
        ($summary.LatestAttitude.PitchDeg -ne -2.5) -or
        ($summary.LatestAttitude.YawDeg -ne 45.0) -or
        ($summary.LatestAttitude.AccelUsed -ne $true) -or
        ($summary.LatestAttitude.TimingResetCount -ne 0)) {
        throw "Attitude telemetry fixture summary did not match expected values."
    }
    $csvRows = @(Import-Csv -LiteralPath $csvPath)
    if (($csvRows.Count -ne 2) -or
        ($csvRows[1].yaw_deg -ne "45") -or
        ($csvRows[1].accel_weight -ne "1") -or
        ($csvRows[1].processed_count -ne "1200")) {
        throw "Attitude telemetry CSV did not match expected values."
    }
    Write-Output "Attitude telemetry fixture: PASS"
}
finally {
    Remove-Item -LiteralPath $binaryPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $jsonPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $csvPath -Force -ErrorAction SilentlyContinue
}
