[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SettingsDumpPath,
    [Parameter(Mandatory = $true)]
    [string]$CalibrationCsvPath,
    [Parameter(Mandatory = $true)]
    [string]$AdapterSerial,
    [ValidateRange(0, 49)]
    [int]$BlackPercentile = 1,
    [ValidateRange(51, 100)]
    [int]$WhitePercentile = 95
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

function Get-Crc32 {
    param([byte[]]$Data, [int]$Length)

    [uint32]$crc = [uint32]::MaxValue
    for ($index = 0; $index -lt $Length; $index++) {
        $crc = $crc -bxor [uint32]$Data[$index]
        for ($bit = 0; $bit -lt 8; $bit++) {
            [uint32]$mask = 0
            if (($crc -band 1) -ne 0) {
                $mask = [uint32]::MaxValue
            }
            $crc = [uint32](($crc -shr 1) -bxor
                (0xEDB88320 -band $mask))
        }
    }
    return [uint32]($crc -bxor [uint32]::MaxValue)
}

$settingsDump = [IO.File]::ReadAllBytes(
    (Resolve-Path -LiteralPath $SettingsDumpPath))
if ($settingsDump.Length -lt 48 -or
    [BitConverter]::ToUInt32($settingsDump, 0) -ne 0x43464745 -or
    [BitConverter]::ToUInt16($settingsDump, 4) -ne 2 -or
    [BitConverter]::ToUInt16($settingsDump, 6) -ne 48 -or
    [BitConverter]::ToUInt32($settingsDump, 40) -ne
        (Get-Crc32 -Data $settingsDump -Length 40)) {
    throw "The source settings record is not a valid version 2 record."
}

$rows = Import-Csv -LiteralPath $CalibrationCsvPath
if ($rows.Count -eq 0) {
    throw "The calibration capture is empty."
}
$black = New-Object uint16[] 8
$white = New-Object uint16[] 8
for ($channel = 0; $channel -lt 8; $channel++) {
    $column = "raw$channel"
    [int[]]$values = $rows | ForEach-Object { [int]$_.$column } |
        Sort-Object
    $blackIndex = [Math]::Floor(
        ($values.Count - 1) * $BlackPercentile / 100.0)
    $whiteIndex = [Math]::Floor(
        ($values.Count - 1) * $WhitePercentile / 100.0)
    $blackValue = $values[$blackIndex]
    $whiteValue = $values[$whiteIndex]
    if (($whiteValue - $blackValue) -lt 180) {
        throw "Channel $channel span is less than 180 counts."
    }
    $black[$channel] = $blackValue
    $white[$channel] = $whiteValue
}

$record = New-Object byte[] 88
for ($index = 0; $index -lt $record.Length; $index++) {
    $record[$index] = 0xFF
}
[BitConverter]::GetBytes([uint32]0x43464745).CopyTo($record, 0)
[BitConverter]::GetBytes([uint16]3).CopyTo($record, 4)
[BitConverter]::GetBytes([uint16]88).CopyTo($record, 6)
$generation = [BitConverter]::ToUInt32($settingsDump, 8) + 1
[BitConverter]::GetBytes([uint32]$generation).CopyTo($record, 8)
[Array]::Copy($settingsDump, 12, $record, 12, 28)
for ($channel = 0; $channel -lt 8; $channel++) {
    [BitConverter]::GetBytes($black[$channel]).CopyTo(
        $record, 40 + 2 * $channel)
    [BitConverter]::GetBytes($white[$channel]).CopyTo(
        $record, 56 + 2 * $channel)
}
$record[72] = 0xFF
$record[73] = 0
$record[74] = 0
$record[75] = 0
$crc = Get-Crc32 -Data $record -Length 76
[BitConverter]::GetBytes($crc).CopyTo($record, 76)

$openOcd = Join-Path $EchoPaths.OpenOcdRoot "bin\openocd.exe"
$openOcdScripts = Join-Path $EchoPaths.OpenOcdRoot "openocd\scripts"
$objcopy = Join-Path $EchoPaths.GnuArmBin "arm-none-eabi-objcopy.exe"
$binaryPath = Join-Path $env:TEMP "echo_reflectance_v3.bin"
$hexPath = Join-Path $env:TEMP "echo_reflectance_v3.hex"
$readbackPath = Join-Path $env:TEMP "echo_reflectance_v3_readback.bin"

try {
    [IO.File]::WriteAllBytes($binaryPath, $record)
    & $objcopy -I binary -O ihex --change-addresses 0x1FC00 `
        $binaryPath $hexPath
    if ($LASTEXITCODE -ne 0) {
        throw "Could not create the calibration image."
    }

    $programCommand = 'program "{0}" reset exit' -f
        $hexPath.Replace("\", "/")
    & $openOcd -s $openOcdScripts -f interface/cmsis-dap.cfg `
        -c ('adapter serial "{0}"' -f $AdapterSerial) `
        -f target/ti_mspm0.cfg -c "adapter speed 1000" `
        -c $programCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Calibration programming failed."
    }

    $dumpCommand = 'dump_image "{0}" 0x0001FC00 88' -f
        $readbackPath.Replace("\", "/")
    & $openOcd -s $openOcdScripts -f interface/cmsis-dap.cfg `
        -c ('adapter serial "{0}"' -f $AdapterSerial) `
        -f target/ti_mspm0.cfg -c "adapter speed 1000" `
        -c init -c "reset halt" -c $dumpCommand `
        -c "reset run" -c shutdown
    if ($LASTEXITCODE -ne 0) {
        throw "Calibration readback failed."
    }

    $expectedHash = (Get-FileHash -LiteralPath $binaryPath `
        -Algorithm SHA256).Hash
    $readbackHash = (Get-FileHash -LiteralPath $readbackPath `
        -Algorithm SHA256).Hash
    if ($expectedHash -ne $readbackHash) {
        throw "Calibration readback did not match the programmed record."
    }

    [pscustomobject]@{
        Version = 3
        Generation = $generation
        ValidMask = "0xFF"
        Black = $black -join ","
        White = $white -join ","
        Crc32 = "0x{0:X8}" -f $crc
        Sha256 = $readbackHash
    }
}
finally {
    Remove-Item -LiteralPath $binaryPath, $hexPath, $readbackPath `
        -Force -ErrorAction SilentlyContinue
}
