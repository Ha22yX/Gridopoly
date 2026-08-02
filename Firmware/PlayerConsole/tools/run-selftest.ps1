[CmdletBinding()]
param(
    [string]$Port = 'COM4',
    [int]$TimeoutSeconds = 35
)

$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
& powershell -ExecutionPolicy Bypass -File (Join-Path $tools 'compile.ps1') -SelfTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& powershell -ExecutionPolicy Bypass -File (Join-Path $tools 'upload.ps1') -Port $Port
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$captured = [Text.StringBuilder]::new()
$serial = $null
try {
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($null -eq $serial) {
            try {
                $serial = [IO.Ports.SerialPort]::new(
                    $Port,
                    115200,
                    [IO.Ports.Parity]::None,
                    8,
                    [IO.Ports.StopBits]::One
                )
                $serial.ReadTimeout = 250
                $serial.DtrEnable = $false
                $serial.RtsEnable = $false
                $serial.Open()
            } catch {
                if ($null -ne $serial) { $serial.Dispose() }
                $serial = $null
                Start-Sleep -Milliseconds 250
                continue
            }
        }

        try {
            $chunk = $serial.ReadExisting()
            if ($chunk.Length -eq 0) {
                Start-Sleep -Milliseconds 50
                continue
            }
            Write-Host -NoNewline $chunk
            [void]$captured.Append($chunk)
            if ($captured.ToString().Contains('SELFTEST FAILED')) { exit 1 }
            if ($captured.ToString().Contains('SELFTEST PASS')) { exit 0 }
        } catch {
            $serial.Close()
            $serial.Dispose()
            $serial = $null
            Start-Sleep -Milliseconds 250
        }
    }
    Write-Error "Timed out waiting for a self-test marker on $Port."
    exit 1
} finally {
    if ($null -ne $serial) {
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
}
