[CmdletBinding()]
param(
    [string]$AdapterSerial = "",
    [ValidateRange(0, 65535)]
    [int]$AdapterVid = 0,
    [ValidateRange(0, 65535)]
    [int]$AdapterPid = 0,
    [ValidateRange(100, 5000)]
    [int]$AdapterSpeedKhz = 1000
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "echo_paths.ps1")

$projectRoot = $EchoPaths.ProjectRoot
$openocd = Join-Path $EchoPaths.OpenOcdRoot "bin\openocd.exe"
$scripts = Join-Path $EchoPaths.OpenOcdRoot "openocd\scripts"
$objcopy = Join-Path $EchoPaths.GnuArmBin "arm-none-eabi-objcopy.exe"
$hex = Join-Path $projectRoot "keil\Objects\ECHO.hex"

foreach ($requiredPath in @($openocd, $scripts, $objcopy, $hex)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required file or directory not found: $requiredPath"
    }
}

if (($AdapterVid -eq 0) -ne ($AdapterPid -eq 0)) {
    throw "AdapterVid and AdapterPid must be specified together."
}

function Invoke-EchoOpenOcd {
    param([string[]]$Commands)

    $arguments = @(
        "-s", $scripts,
        "-f", "interface/cmsis-dap.cfg"
    )
    if ($AdapterVid -ne 0) {
        $arguments += @(
            "-c",
            ('cmsis-dap vid_pid 0x{0:X4} 0x{1:X4}' -f `
                $AdapterVid, $AdapterPid)
        )
    }
    if (-not [string]::IsNullOrWhiteSpace($AdapterSerial)) {
        $arguments += @("-c", ('adapter serial "{0}"' -f $AdapterSerial))
    }
    $arguments += @(
        "-f", "target/ti_mspm0.cfg",
        "-c", "adapter speed $AdapterSpeedKhz"
    )
    foreach ($command in $Commands) {
        $arguments += @("-c", $command)
    }
    & $openocd @arguments
}

$hexForTcl = $hex.Replace("\", "/")
$programCommand = 'program "{0}" reset exit' -f $hexForTcl

Write-Host "Flashing ECHO through DAPLink..." -ForegroundColor Cyan
Write-Host "Image: $hex"
if (-not [string]::IsNullOrWhiteSpace($AdapterSerial)) {
    Write-Host "CMSIS-DAP serial: $AdapterSerial"
}
if ($AdapterVid -ne 0) {
    Write-Host ("CMSIS-DAP VID:PID: {0:X4}:{1:X4}" -f `
        $AdapterVid, $AdapterPid)
}
Write-Host "SWD frequency: $AdapterSpeedKhz kHz"

Invoke-EchoOpenOcd -Commands @($programCommand)
if ($LASTEXITCODE -ne 0) {
    throw "OpenOCD Flash programming failed with exit code $LASTEXITCODE."
}

Write-Host "Programming succeeded; starting byte-for-byte Flash readback verification." -ForegroundColor Cyan

$tempRoot = [System.IO.Path]::GetTempPath()
$expectedBinary = Join-Path $tempRoot ("ECHO_expected_{0}.bin" -f $PID)
$readbackBinary = Join-Path $tempRoot ("ECHO_readback_{0}.bin" -f $PID)

try {
    & $objcopy -I ihex -O binary $hex $expectedBinary
    if ($LASTEXITCODE -ne 0) {
        throw "objcopy could not convert ECHO.hex for readback verification."
    }

    $imageLength = (Get-Item -LiteralPath $expectedBinary).Length
    if ($imageLength -le 0) {
        throw "ECHO.hex produced an empty binary image."
    }

    $readbackForTcl = $readbackBinary.Replace("\", "/")
    $dumpCommand = 'dump_image "{0}" 0x00000000 {1}' -f `
        $readbackForTcl, $imageLength

    Invoke-EchoOpenOcd -Commands @(
        "init",
        "reset halt",
        $dumpCommand,
        "shutdown"
    )
    if ($LASTEXITCODE -ne 0) {
        throw "OpenOCD Flash readback failed with exit code $LASTEXITCODE."
    }

    $expectedHash = (Get-FileHash -LiteralPath $expectedBinary `
        -Algorithm SHA256).Hash
    $readbackHash = (Get-FileHash -LiteralPath $readbackBinary `
        -Algorithm SHA256).Hash
    if ($expectedHash -ne $readbackHash) {
        throw @"
Flash readback mismatch.
Expected: $expectedHash
Readback: $readbackHash
"@
    }

    Invoke-EchoOpenOcd -Commands @("init", "reset run", "shutdown")
    if ($LASTEXITCODE -ne 0) {
        throw "Flash verified, but target reset failed with exit code $LASTEXITCODE."
    }

    Write-Host "Flash readback SHA-256: $readbackHash"
    Write-Host "Flash and byte-for-byte verification succeeded." -ForegroundColor Green
}
finally {
    Remove-Item -LiteralPath $expectedBinary, $readbackBinary `
        -Force -ErrorAction SilentlyContinue
}
