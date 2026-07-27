[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM\d+$')]
    [string]$Port,
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$InputPath,
    [ValidateNotNullOrEmpty()]
    [string]$RemotePath = "main.py",
    [ValidateRange(64, 512)]
    [int]$ChunkBytes = 192
)

$ErrorActionPreference = "Stop"

function Read-UntilPrompt {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$TimeoutMs = 3000
    )

    $deadline = [Environment]::TickCount64 + $TimeoutMs
    $text = ""
    while ([Environment]::TickCount64 -lt $deadline) {
        $text += $Serial.ReadExisting()
        if ($text.Contains(">>> ")) {
            return $text
        }
        Start-Sleep -Milliseconds 10
    }
    throw "Timed out waiting for the MicroPython prompt on $($Serial.PortName)."
}

function Invoke-ReplLine {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Line,
        [int]$TimeoutMs = 3000
    )

    $Serial.Write($Line + "`r`n")
    $output = Read-UntilPrompt -Serial $Serial -TimeoutMs $TimeoutMs
    if ($output.Contains("Traceback (most recent call last):")) {
        throw "MicroPython command failed:`n$output"
    }
    return $output
}

$sourcePath = (Resolve-Path -LiteralPath $InputPath).Path
$sourceBytes = [System.IO.File]::ReadAllBytes($sourcePath)
$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
$serial = [System.IO.Ports.SerialPort]::new(
    $Port,
    115200,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.ReadTimeout = 500
$serial.WriteTimeout = 2000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    $serial.Open()
    $serial.BaseStream.Write([byte[]](3, 3, 2, 13, 10), 0, 5)
    [void](Read-UntilPrompt -Serial $serial -TimeoutMs 4000)
    $serial.DiscardInBuffer()

    [void](Invoke-ReplLine -Serial $serial -Line "import ubinascii")
    [void](Invoke-ReplLine -Serial $serial -Line (
        "f=open({0},'wb')" -f (ConvertTo-Json $RemotePath -Compress)
    ))

    for ($offset = 0; $offset -lt $sourceBytes.Length; $offset += $ChunkBytes) {
        $count = [Math]::Min($ChunkBytes, $sourceBytes.Length - $offset)
        $chunk = [byte[]]::new($count)
        [Array]::Copy($sourceBytes, $offset, $chunk, 0, $count)
        $encoded = [Convert]::ToBase64String($chunk)
        [void](Invoke-ReplLine -Serial $serial -Line (
            "f.write(ubinascii.a2b_base64(b'{0}'))" -f $encoded
        ))
    }
    [void](Invoke-ReplLine -Serial $serial -Line "f.close()")

    $verifyLine = "import hashlib; d=open({0},'rb').read(); print('CODEX_UPLOAD',len(d),ubinascii.hexlify(hashlib.sha256(d).digest()).decode())" -f `
        (ConvertTo-Json $RemotePath -Compress)
    $verifyOutput = Invoke-ReplLine -Serial $serial -Line $verifyLine -TimeoutMs 5000
    if (-not $verifyOutput.Contains("CODEX_UPLOAD $($sourceBytes.Length) $sourceHash")) {
        throw "MicroPython upload verification failed:`n$verifyOutput"
    }

    Write-Host "Uploaded $sourcePath to ${Port}:$RemotePath"
    Write-Host "Bytes: $($sourceBytes.Length)"
    Write-Host "SHA-256: $sourceHash"
    $serial.Write("import machine; machine.reset()`r`n")
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
}
