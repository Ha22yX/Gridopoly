[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'selftest-runner.psm1'
Import-Module $modulePath -Force

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) { throw $Message }
}

$fixtureDir = Join-Path ([IO.Path]::GetTempPath()) ("gridopoly-selftest-" + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $fixtureDir | Out-Null
try {
    $sleepingChild = Join-Path $fixtureDir 'sleeping-child.ps1'
    @'
$child = Start-Process -FilePath powershell -ArgumentList '-NoProfile -Command "Start-Sleep -Seconds 20"' -PassThru
Write-Output "phase started"
Write-Output "CHILD_PID=$($child.Id)"
Start-Sleep -Seconds 20
'@ | Set-Content -LiteralPath $sleepingChild -NoNewline

    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    $phaseError = $null
    try {
        Invoke-SelfTestPhase -Name 'compile' -ScriptPath $sleepingChild -ScriptArguments @() -TimeoutSeconds 1
    } catch {
        $phaseError = $_
    }
    $stopwatch.Stop()

    Assert-True ($null -ne $phaseError) 'compile phase did not time out'
    Assert-True ($stopwatch.Elapsed.TotalSeconds -lt 5) 'compile phase timeout was not bounded'
    Assert-True ($phaseError.Exception.Message.Contains('phase started')) "compile phase output was not surfaced on timeout: $($phaseError.Exception.Message)"
    $childId = [int]([regex]::Match($phaseError.Exception.Message, 'CHILD_PID=(\d+)').Groups[1].Value)
    Assert-True ($childId -gt 0) 'compile phase child PID was not retained in timeout output'
    Start-Sleep -Milliseconds 250
    Assert-True ($null -eq (Get-Process -Id $childId -ErrorAction SilentlyContinue)) 'compile phase descendant survived timeout termination'

    $unresponsivePhase = Join-Path $fixtureDir 'unresponsive-phase.ps1'
    @'
Write-Output "PHASE_PID=$PID"
Start-Sleep -Seconds 20
'@ | Set-Content -LiteralPath $unresponsivePhase -NoNewline
    $unresponsivePid = 0
    try {
        $stopwatch = [Diagnostics.Stopwatch]::StartNew()
        $terminationError = $null
        try {
            Invoke-SelfTestPhase -Name 'termination-failure' -ScriptPath $unresponsivePhase -ScriptArguments @() -TimeoutSeconds 1 -TerminationGraceMilliseconds 250 -TerminateProcessTree { param($processId, $graceMilliseconds) $false }
        } catch {
            $terminationError = $_
        }
        $stopwatch.Stop()

        Assert-True ($null -ne $terminationError) 'failed process-tree termination did not return an error'
        Assert-True ($stopwatch.Elapsed.TotalSeconds -lt 3) 'failed process-tree termination exceeded the hard post-timeout bound'
        Assert-True ($terminationError.Exception.Message.Contains('could not terminate')) 'failed process-tree termination did not report its bounded failure'
        $unresponsivePid = [int]([regex]::Match($terminationError.Exception.Message, 'PHASE_PID=(\d+)').Groups[1].Value)
        Assert-True ($unresponsivePid -gt 0) 'failed process-tree termination did not retain phase output'
    } finally {
        if ($unresponsivePid -gt 0) {
            & taskkill.exe /PID $unresponsivePid /T /F 2>$null | Out-Null
        }
    }

    $highVolumePhase = Join-Path $fixtureDir 'high-volume-phase.ps1'
    @'
for ($index = 0; $index -lt 4096; $index++) {
    Write-Output ('V' * 256)
}
exit 7
'@ | Set-Content -LiteralPath $highVolumePhase -NoNewline
    $highVolumeError = $null
    try {
        Invoke-SelfTestPhase -Name 'high-volume' -ScriptPath $highVolumePhase -ScriptArguments @() -TimeoutSeconds 5 -OutputTailLength 128 -SuppressPhaseOutput
    } catch {
        $highVolumeError = $_
    }
    Assert-True ($null -ne $highVolumeError) 'high-volume phase did not report its non-zero exit'
    Assert-True ($highVolumeError.Exception.Message.Length -lt 512) 'high-volume phase retained more than the configured diagnostic output cap'
    Assert-True ($highVolumeError.Exception.Message.Contains(('V' * 64))) 'high-volume phase did not retain the most recent output tail'

    $markerWindow = ''
    for ($index = 0; $index -lt 256; $index++) {
        $markerWindow = Add-SelfTestMarkerText -Window $markerWindow -Chunk ('x' * 1024) -MaximumLength 128
    }
    $markerWindow = Add-SelfTestMarkerText -Window $markerWindow -Chunk 'SELFTEST PA' -MaximumLength 128
    $markerWindow = Add-SelfTestMarkerText -Window $markerWindow -Chunk 'SS' -MaximumLength 128
    Assert-True ($markerWindow.Length -le 128) 'serial marker storage exceeded its fixed window'
    Assert-True ($markerWindow.Contains('SELFTEST PASS')) 'serial marker window lost a split pass marker'

    $markerWindow = Add-SelfTestMarkerText -Window $markerWindow -Chunk ('x' * 128) -MaximumLength 128
    $markerWindow = Add-SelfTestMarkerText -Window $markerWindow -Chunk 'SELFTEST FAI' -MaximumLength 128
    $markerWindow = Add-SelfTestMarkerText -Window $markerWindow -Chunk 'LED' -MaximumLength 128
    Assert-True ($markerWindow.Length -le 128) 'serial marker storage grew after additional noise'
    Assert-True ($markerWindow.Contains('SELFTEST FAILED')) 'serial marker window lost a split failure marker'

    Write-Host 'SELFTEST RUNNER REGRESSION TEST PASS'
} finally {
    if (Test-Path -LiteralPath $fixtureDir) {
        Remove-Item -LiteralPath $fixtureDir -Recurse -Force
    }
}
