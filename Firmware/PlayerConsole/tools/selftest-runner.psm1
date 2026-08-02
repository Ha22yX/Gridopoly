function Add-SelfTestMarkerText {
    [CmdletBinding()]
    param(
        [AllowEmptyString()]
        [string]$Window,
        [AllowEmptyString()]
        [string]$Chunk,
        [ValidateRange(1, 4096)]
        [int]$MaximumLength
    )

    $combined = $Window + $Chunk
    if ($combined.Length -le $MaximumLength) { return $combined }
    return $combined.Substring($combined.Length - $MaximumLength)
}

function Invoke-SelfTestPhase {
    [CmdletBinding()]
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string[]]$ScriptArguments,
        [ValidateRange(1, 2147483)]
        [int]$TimeoutSeconds
    )

    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = 'powershell.exe'
    $process.StartInfo.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$ScriptPath`" $($ScriptArguments -join ' ')"
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true

    try {
        if (-not $process.Start()) { throw "Unable to start $Name phase." }
        $standardOutputTask = $process.StandardOutput.ReadToEndAsync()
        $standardErrorTask = $process.StandardError.ReadToEndAsync()
        $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
        if ($timedOut) {
            & taskkill.exe /PID $process.Id /T /F 2>$null | Out-Null
        }
        $process.WaitForExit()
        $phaseOutput = Write-SelfTestPhaseOutput -StandardOutput $standardOutputTask.Result -StandardError $standardErrorTask.Result
        if ($timedOut) {
            throw "$Name phase timed out after $TimeoutSeconds seconds. Output before termination:`n$phaseOutput"
        }
        if ($process.ExitCode -ne 0) {
            throw "$Name phase failed with exit code $($process.ExitCode). Output:`n$phaseOutput"
        }
    } finally {
        if (-not $process.HasExited) {
            & taskkill.exe /PID $process.Id /T /F 2>$null | Out-Null
            $process.WaitForExit()
        }
        $process.Dispose()
    }
}

function Write-SelfTestPhaseOutput {
    [CmdletBinding()]
    param(
        [string]$StandardOutput,
        [string]$StandardError
    )

    $tail = ''
    foreach ($text in @($StandardOutput, $StandardError)) {
        if ([string]::IsNullOrEmpty($text)) { continue }
        Write-Host -NoNewline $text
        if ($text.Length -gt 4096) {
            $text = $text.Substring($text.Length - 4096)
        }
        $tail = Add-SelfTestMarkerText -Window $tail -Chunk $text -MaximumLength 4096
    }
    return $tail
}

Export-ModuleMember -Function Add-SelfTestMarkerText, Invoke-SelfTestPhase
