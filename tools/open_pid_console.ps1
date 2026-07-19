param(
    [ValidateRange(1024, 65535)]
    [int]$Port = 8765,
    [string]$SerialPort = "COM9",
    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$BaudRate = 230400,
    [switch]$NoBrowser
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$bridgeScript = Join-Path $PSScriptRoot "telemetry_bridge.py"
$url = "http://127.0.0.1:$Port/"

function Get-PidBridgeStatus {
    try {
        $status = Invoke-RestMethod `
            -Uri ($url + "api/serial/status") `
            -TimeoutSec 1
        if ($status.bridge -eq $true) {
            return $status
        }
        return $null
    }
    catch {
        return $null
    }
}

function Test-PortOpen {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $connection = $client.BeginConnect("127.0.0.1", $Port, $null, $null)
        return $connection.AsyncWaitHandle.WaitOne(250, $false) -and
            $client.Connected
    }
    catch {
        return $false
    }
    finally {
        $client.Dispose()
    }
}

$bridgeStatus = Get-PidBridgeStatus
if ($null -eq $bridgeStatus) {
    if (Test-PortOpen) {
        throw "Port $Port is occupied by another service."
    }

    $python = Get-Command python.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.Source -notlike "*WindowsApps*" } |
        Select-Object -First 1
    if ($null -eq $python) {
        $python = Get-Command py.exe -ErrorAction SilentlyContinue |
            Select-Object -First 1
    }
    if ($null -eq $python) {
        throw "Python was not found."
    }
    if (-not (Test-Path -LiteralPath $bridgeScript -PathType Leaf)) {
        throw "PID serial bridge was not found: $bridgeScript"
    }

    $arguments = @(
        "`"$bridgeScript`"",
        "--http-port", "$Port",
        "--serial-port", "$SerialPort",
        "--baud-rate", "$BaudRate"
    )
    $server = Start-Process -FilePath $python.Source `
        -ArgumentList $arguments -WorkingDirectory $projectRoot `
        -WindowStyle Hidden -PassThru

    $ready = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        Start-Sleep -Milliseconds 100
        if ($server.HasExited) {
            throw "PID console server exited with code $($server.ExitCode)."
        }
        $bridgeStatus = Get-PidBridgeStatus
        if ($null -ne $bridgeStatus) {
            $ready = $true
            break
        }
    }
    if (-not $ready) {
        throw "PID console server did not become ready at $url"
    }
}
if ($bridgeStatus.connected -ne $true) {
    throw "PID bridge is running, but $SerialPort is unavailable: $($bridgeStatus.error)"
}

if (-not $NoBrowser) {
    $browserCandidates = @(
        "C:\Program Files\Google\Chrome\Application\chrome.exe",
        "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
        "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        "C:\Program Files\Microsoft\Edge\Application\msedge.exe"
    )
    $browser = $browserCandidates |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
    if ($null -ne $browser) {
        Start-Process -FilePath $browser -ArgumentList $url
    }
    else {
        Start-Process $url
    }
}

Write-Host "ECHO PID Console: $url" -ForegroundColor Cyan
Write-Host "Serial bridge: $SerialPort @ $BaudRate (kept open across page reloads)"
Write-Host "Stop the bridge before using command-line COM-port tools."
