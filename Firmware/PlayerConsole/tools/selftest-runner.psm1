if ($null -eq ('Gridopoly.BoundedPhaseOutput' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Text;

namespace Gridopoly {
    public sealed class BoundedPhaseOutput {
        private readonly int capacity;
        private readonly object sync = new object();
        private readonly StringBuilder standardOutput = new StringBuilder();
        private readonly StringBuilder standardError = new StringBuilder();

        public bool SuppressOutput { get; set; }
        public volatile bool StandardOutputComplete;
        public volatile bool StandardErrorComplete;
        public volatile bool SawSelfTestPass;
        public volatile bool SawSelfTestFailed;
        public DataReceivedEventHandler StandardOutputHandler { get { return new DataReceivedEventHandler(OnStandardOutput); } }
        public DataReceivedEventHandler StandardErrorHandler { get { return new DataReceivedEventHandler(OnStandardError); } }

        public BoundedPhaseOutput(int capacity, bool suppressOutput) {
            this.capacity = capacity;
            SuppressOutput = suppressOutput;
        }

        public void OnStandardOutput(object sender, DataReceivedEventArgs eventArgs) {
            Receive(standardOutput, eventArgs, false);
        }

        public void OnStandardError(object sender, DataReceivedEventArgs eventArgs) {
            Receive(standardError, eventArgs, true);
        }

        public string GetTail() {
            lock (sync) {
                string combined = standardOutput.ToString() + standardError.ToString();
                return combined.Length <= capacity ? combined : combined.Substring(combined.Length - capacity);
            }
        }

        private void Receive(StringBuilder target, DataReceivedEventArgs eventArgs, bool isError) {
            if (eventArgs.Data == null) {
                if (isError) StandardErrorComplete = true;
                else StandardOutputComplete = true;
                return;
            }

            lock (sync) {
                AppendTail(target, eventArgs.Data + Environment.NewLine);
                SawSelfTestPass |= eventArgs.Data.Contains("SELFTEST PASS");
                SawSelfTestFailed |= eventArgs.Data.Contains("SELFTEST FAILED");
            }
            if (!SuppressOutput) Console.Out.WriteLine(eventArgs.Data);
        }

        private void AppendTail(StringBuilder target, string text) {
            if (text.Length >= capacity) {
                target.Clear();
                target.Append(text.Substring(text.Length - capacity));
                return;
            }

            int overflow = target.Length + text.Length - capacity;
            if (overflow > 0) target.Remove(0, overflow);
            target.Append(text);
        }
    }
}
'@
}

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

function Invoke-SelfTestProcessTreeTermination {
    [CmdletBinding()]
    param(
        [int]$ProcessId,
        [ValidateRange(1, 60000)]
        [int]$GraceMilliseconds
    )

    $terminator = [Diagnostics.Process]::new()
    $terminator.StartInfo.FileName = Join-Path $env:SystemRoot 'System32\taskkill.exe'
    $terminator.StartInfo.Arguments = "/PID $ProcessId /T /F"
    $terminator.StartInfo.UseShellExecute = $false
    $terminator.StartInfo.CreateNoWindow = $true
    try {
        if (-not $terminator.Start()) { return $false }
        if (-not $terminator.WaitForExit($GraceMilliseconds)) {
            try { $terminator.Kill() } catch {}
            return $false
        }
        return $terminator.ExitCode -eq 0
    } finally {
        $terminator.Dispose()
    }
}

function Wait-SelfTestPhaseStreams {
    [CmdletBinding()]
    param(
        [Gridopoly.BoundedPhaseOutput]$Capture,
        [ValidateRange(1, 60000)]
        [int]$GraceMilliseconds
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($GraceMilliseconds)
    while (-not ($Capture.StandardOutputComplete -and $Capture.StandardErrorComplete)) {
        if ([DateTime]::UtcNow -ge $deadline) { return $false }
        Start-Sleep -Milliseconds 10
    }
    return $true
}

function Invoke-SelfTestPhase {
    [CmdletBinding()]
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string[]]$ScriptArguments,
        [ValidateRange(1, 2147483)]
        [int]$TimeoutSeconds,
        [ValidateRange(1, 60000)]
        [int]$TerminationGraceMilliseconds = 1000,
        [ValidateRange(64, 65536)]
        [int]$OutputTailLength = 4096,
        [ValidateRange(1, 60000)]
        [int]$StreamDrainTimeoutMilliseconds = 15000,
        [switch]$SuppressPhaseOutput,
        [scriptblock]$TerminateProcessTree
    )

    $capture = [Gridopoly.BoundedPhaseOutput]::new($OutputTailLength, $SuppressPhaseOutput)
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = 'powershell.exe'
    $process.StartInfo.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$ScriptPath`" $($ScriptArguments -join ' ')"
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $outputHandler = $capture.StandardOutputHandler
    $errorHandler = $capture.StandardErrorHandler
    $outputReadStarted = $false
    $errorReadStarted = $false
    $processStarted = $false
    $terminationAttempted = $false

    try {
        $process.add_OutputDataReceived($outputHandler)
        $process.add_ErrorDataReceived($errorHandler)
        if (-not $process.Start()) { throw "Unable to start $Name phase." }
        $processStarted = $true
        $process.BeginOutputReadLine()
        $outputReadStarted = $true
        $process.BeginErrorReadLine()
        $errorReadStarted = $true

        $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
        if ($timedOut) {
            $terminationAttempted = $true
            $terminationDeadline = [DateTime]::UtcNow.AddMilliseconds($TerminationGraceMilliseconds)
            if ($null -ne $TerminateProcessTree) {
                $terminationSucceeded = [bool](& $TerminateProcessTree $process.Id $TerminationGraceMilliseconds)
            } else {
                $terminationSucceeded = Invoke-SelfTestProcessTreeTermination -ProcessId $process.Id -GraceMilliseconds $TerminationGraceMilliseconds
            }
            $remainingGrace = [Math]::Max(0, [int]($terminationDeadline - [DateTime]::UtcNow).TotalMilliseconds)
            $processExited = $process.WaitForExit($remainingGrace)
            if ($processExited) {
                [void](Wait-SelfTestPhaseStreams -Capture $capture -GraceMilliseconds $TerminationGraceMilliseconds)
            }
            $phaseOutput = $capture.GetTail()
            if (-not $terminationSucceeded) {
                throw "$Name phase could not terminate its process tree within $TerminationGraceMilliseconds milliseconds. Output before termination:`n$phaseOutput"
            }
            if (-not $processExited) {
                throw "$Name phase process tree did not exit within $TerminationGraceMilliseconds milliseconds. Output before termination:`n$phaseOutput"
            }
            throw "$Name phase timed out after $TimeoutSeconds seconds. Output before termination:`n$phaseOutput"
        }

        [void](Wait-SelfTestPhaseStreams -Capture $capture -GraceMilliseconds $StreamDrainTimeoutMilliseconds)
        $phaseOutput = $capture.GetTail()
        if ($process.ExitCode -ne 0) {
            throw "$Name phase failed with exit code $($process.ExitCode). Output:`n$phaseOutput"
        }
        return [PSCustomObject]@{
            OutputTail = $phaseOutput
            SawSelfTestPass = $capture.SawSelfTestPass
            SawSelfTestFailed = $capture.SawSelfTestFailed
        }
    } finally {
        if ($processStarted -and -not $process.HasExited -and -not $terminationAttempted) {
            [void](Invoke-SelfTestProcessTreeTermination -ProcessId $process.Id -GraceMilliseconds $TerminationGraceMilliseconds)
            [void]$process.WaitForExit($TerminationGraceMilliseconds)
        }
        if ($outputReadStarted) { try { $process.CancelOutputRead() } catch {} }
        if ($errorReadStarted) { try { $process.CancelErrorRead() } catch {} }
        $process.remove_OutputDataReceived($outputHandler)
        $process.remove_ErrorDataReceived($errorHandler)
        $process.Dispose()
    }
}

Export-ModuleMember -Function Add-SelfTestMarkerText, Invoke-SelfTestPhase
