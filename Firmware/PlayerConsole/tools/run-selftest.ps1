[CmdletBinding()]
param(
    [string]$Port = 'COM4',
    [int]$TimeoutSeconds = 35,
    [int]$CompileTimeoutSeconds = 180,
    [int]$UploadTimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
Import-Module (Join-Path $tools 'selftest-runner.psm1') -Force
Invoke-SelfTestPhase -Name 'compile' -ScriptPath (Join-Path $tools 'compile.ps1') -ScriptArguments @('-SelfTest') -TimeoutSeconds $CompileTimeoutSeconds
Invoke-SelfTestPhase -Name 'upload' -ScriptPath (Join-Path $tools 'upload.ps1') -ScriptArguments @('-Port', $Port) -TimeoutSeconds $UploadTimeoutSeconds

$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$captured = ''
$markerWindowSize = 128
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
            $captured = Add-SelfTestMarkerText -Window $captured -Chunk $chunk -MaximumLength $markerWindowSize
            if ($captured.Contains('SELFTEST FAILED')) { exit 1 }
            if ($captured.Contains('SELFTEST PASS')) { exit 0 }
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
