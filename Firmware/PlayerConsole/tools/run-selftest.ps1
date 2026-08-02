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
$compileOutput = Invoke-SelfTestPhase -Name 'compile' -ScriptPath (Join-Path $tools 'compile.ps1') -ScriptArguments @('-SelfTest') -TimeoutSeconds $CompileTimeoutSeconds
$uploadOutput = Invoke-SelfTestPhase -Name 'upload' -ScriptPath (Join-Path $tools 'upload.ps1') -ScriptArguments @('-Port', $Port) -TimeoutSeconds $UploadTimeoutSeconds
if ($uploadOutput.SawSelfTestFailed) { exit 1 }
if ($uploadOutput.SawSelfTestPass) { exit 0 }

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
            if ($chunk -match 'SELFTEST\s+FAILED') { exit 1 }
            if ($chunk -match 'SELFTEST\s+PASS') { exit 0 }
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
