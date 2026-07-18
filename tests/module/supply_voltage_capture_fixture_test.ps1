$ErrorActionPreference = "Stop"

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot "..\.."))
$captureTool = Join-Path $projectRoot "tools\telemetry_capture.ps1"
$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) (
    "echo-supply-fixture-{0}" -f [Guid]::NewGuid().ToString("N"))
$binaryPath = Join-Path $temporaryDirectory "supply.bin"
$jsonPath = Join-Path $temporaryDirectory "supply.json"

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

try {
    New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
    $frame = New-Object byte[] 40
    $frame[0] = 0xA5
    $frame[1] = 0x5A
    $frame[2] = 1
    $frame[3] = 9
    [BitConverter]::GetBytes([uint16]24).CopyTo($frame, 4)
    [BitConverter]::GetBytes([uint32]7).CopyTo($frame, 6)
    [BitConverter]::GetBytes([uint32]100000).CopyTo($frame, 10)
    [BitConverter]::GetBytes([uint32]123).CopyTo($frame, 14)
    [BitConverter]::GetBytes([uint16]3600).CopyTo($frame, 18)
    [BitConverter]::GetBytes([uint16]3590).CopyTo($frame, 20)
    [BitConverter]::GetBytes([uint16]2893).CopyTo($frame, 22)
    [BitConverter]::GetBytes([uint16]0).CopyTo($frame, 24)
    [BitConverter]::GetBytes([uint32]16037).CopyTo($frame, 26)
    [BitConverter]::GetBytes([uint32]123).CopyTo($frame, 30)
    [BitConverter]::GetBytes([uint32]0).CopyTo($frame, 34)
    $crc = Get-Crc16Ccitt -Data $frame -Offset 2 -Length 36
    [BitConverter]::GetBytes($crc).CopyTo($frame, 38)
    [System.IO.File]::WriteAllBytes($binaryPath, $frame)

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File $captureTool `
        -InputPath $binaryPath -JsonPath $jsonPath | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "telemetry_capture.ps1 rejected the supply fixture."
    }
    $summary = Get-Content -Raw -LiteralPath $jsonPath | ConvertFrom-Json
    if (($summary.SupplyVoltageFrames -ne 1) -or
        ($summary.UnknownFrames -ne 0) -or
        ($summary.CrcErrors -ne 0) -or
        ($summary.LatestSupplyVoltage.SampleSequence -ne 123) -or
        ($summary.LatestSupplyVoltage.Raw -ne 3600) -or
        ($summary.LatestSupplyVoltage.FilteredRaw -ne 3590) -or
        ($summary.LatestSupplyVoltage.AdcInputMv -ne 2893) -or
        ($summary.LatestSupplyVoltage.BatteryMv -ne 16037) -or
        ($summary.LatestSupplyVoltage.ConversionTimeoutCount -ne 0)) {
        throw "Decoded supply voltage fixture did not match."
    }
    Write-Host "Supply voltage telemetry fixture passed."
}
finally {
    Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force `
        -ErrorAction SilentlyContinue
}
