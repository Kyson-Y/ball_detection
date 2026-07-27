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

function New-ImuPayload {
    param([uint32]$SampleSequence, [bool]$Extended = $false)

    $payloadLength = if ($Extended) { 88 } else { 64 }
    $payload = New-Object byte[] $payloadLength
    [BitConverter]::GetBytes($SampleSequence).CopyTo($payload, 0)
    [BitConverter]::GetBytes([uint32]500000).CopyTo($payload, 4)
    [BitConverter]::GetBytes([uint32]70).CopyTo($payload, 8)
    [BitConverter]::GetBytes([uint32]2468).CopyTo($payload, 12)
    [BitConverter]::GetBytes([single]0.125).CopyTo($payload, 16)
    [BitConverter]::GetBytes([single]-0.25).CopyTo($payload, 20)
    [BitConverter]::GetBytes([single]1.0).CopyTo($payload, 24)
    [BitConverter]::GetBytes([single]1.03833).CopyTo($payload, 28)
    [BitConverter]::GetBytes([single]0.01).CopyTo($payload, 32)
    [BitConverter]::GetBytes([single]-0.02).CopyTo($payload, 36)
    [BitConverter]::GetBytes([single]0.03).CopyTo($payload, 40)
    [BitConverter]::GetBytes([single]30.25).CopyTo($payload, 44)
    [BitConverter]::GetBytes([uint16]300).CopyTo($payload, 48)
    [BitConverter]::GetBytes([uint16]300).CopyTo($payload, 50)
    $payload[52] = 4
    $payload[53] = 0x68
    $payload[54] = 0x70
    $payload[55] = 0x0F
    [BitConverter]::GetBytes($SampleSequence).CopyTo($payload, 56)
    [BitConverter]::GetBytes([uint32]0).CopyTo($payload, 60)
    if ($Extended) {
        [BitConverter]::GetBytes([single]1.25).CopyTo($payload, 64)
        [BitConverter]::GetBytes([single]-2.5).CopyTo($payload, 68)
        [BitConverter]::GetBytes([single]3.75).CopyTo($payload, 72)
        [BitConverter]::GetBytes([single]1.0).CopyTo($payload, 76)
        [BitConverter]::GetBytes([single]-2.0).CopyTo($payload, 80)
        [BitConverter]::GetBytes([single]3.0).CopyTo($payload, 84)
    }
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
    $frame[3] = 11
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
    "echo-imu-telemetry-fixture.bin"
$jsonPath = Join-Path ([System.IO.Path]::GetTempPath()) `
    "echo-imu-telemetry-fixture.json"
$csvPath = Join-Path ([System.IO.Path]::GetTempPath()) `
    "echo-imu-telemetry-fixture.csv"
$bytes = [System.Collections.Generic.List[byte]]::new()

try {
    $bytes.AddRange((New-Frame -Payload (New-ImuPayload 1000) `
        -Sequence 0 -TimestampUs 0))
    $bytes.AddRange((New-Frame -Payload (New-ImuPayload 1005 $true) `
        -Sequence 1 -TimestampUs 50000))
    [System.IO.File]::WriteAllBytes($binaryPath, $bytes.ToArray())

    & $capturePath -InputPath $binaryPath -ImuCsvPath $csvPath `
        -JsonPath $jsonPath | Out-Host
    if (($null -ne $LASTEXITCODE) -and ($LASTEXITCODE -ne 0)) {
        throw "telemetry_capture.ps1 returned $LASTEXITCODE"
    }

    $summary = Get-Content -Raw -LiteralPath $jsonPath | ConvertFrom-Json
    if (($summary.ValidFrames -ne 2) -or
        ($summary.ImuFrames -ne 2) -or
        ($summary.ImuTelemetryRateHz -ne 20.0) -or
        ($summary.ImuSampleRateHz -ne 100.0) -or
        ($summary.LatestImu.StateName -ne "READY") -or
        ($summary.LatestImu.Address -ne "0x68") -or
        ($summary.LatestImu.WhoAmI -ne "0x70") -or
        ($summary.LatestImu.Online -ne $true) -or
        ($summary.LatestImu.Valid -ne $true) -or
        ($summary.LatestImu.Calibrated -ne $true) -or
        ($summary.LatestImu.TemperatureC -ne 30.25) -or
        ($summary.LatestImu.GyroRawDps[0] -ne 1.25) -or
        ($summary.LatestImu.GyroBiasDps[1] -ne -2.0) -or
        ($summary.LatestImu.GyroUnfilteredDps[2] -ne 0.75) -or
        ($summary.LatestImu.SampleFailureCount -ne 0)) {
        throw "IMU telemetry fixture summary did not match expected values."
    }
    $csvRows = @(Import-Csv -LiteralPath $csvPath)
    if (($csvRows.Count -ne 2) -or
        ($csvRows[0].gyro_raw_x_dps -ne "") -or
        ($csvRows[1].gyro_raw_x_dps -ne "1.25") -or
        ($csvRows[1].gyro_unfiltered_y_dps -ne "-0.5") -or
        ($csvRows[1].gyro_bias_z_dps -ne "3")) {
        throw "IMU telemetry CSV did not match expected values."
    }
    Write-Output "IMU telemetry fixture: PASS"
}
finally {
    Remove-Item -LiteralPath $binaryPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $jsonPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $csvPath -Force -ErrorAction SilentlyContinue
}
