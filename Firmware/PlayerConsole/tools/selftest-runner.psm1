if ($null -eq ('Gridopoly.BoundedPhaseOutput' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading.Tasks;

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

        public BoundedPhaseOutput(int capacity, bool suppressOutput) {
            this.capacity = capacity;
            SuppressOutput = suppressOutput;
        }

        public async Task ReadAsync(StreamReader reader, bool isError) {
            char[] buffer = new char[4096];
            StringBuilder target = isError ? standardError : standardOutput;
            try {
                while (true) {
                    int count = await reader.ReadAsync(buffer, 0, buffer.Length).ConfigureAwait(false);
                    if (count == 0) break;
                    Receive(target, new string(buffer, 0, count), isError);
                }
            } finally {
                if (isError) StandardErrorComplete = true;
                else StandardOutputComplete = true;
            }
        }

        public string GetTail() {
            lock (sync) {
                int perStreamCapacity = capacity / 2;
                string output = standardOutput.ToString();
                string error = standardError.ToString();
                if (output.Length > perStreamCapacity) output = output.Substring(output.Length - perStreamCapacity);
                if (error.Length > perStreamCapacity) error = error.Substring(error.Length - perStreamCapacity);
                return output + error;
            }
        }

        private void Receive(StringBuilder target, string text, bool isError) {
            lock (sync) {
                AppendTail(target, text);
                SawSelfTestPass |= text.Contains("SELFTEST PASS");
                SawSelfTestFailed |= text.Contains("SELFTEST FAILED");
            }
            if (!SuppressOutput) Console.Out.Write(text);
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
        [Threading.Tasks.Task[]]$Tasks,
        [ValidateRange(1, 60000)]
        [int]$GraceMilliseconds
    )

    return [Threading.Tasks.Task]::WaitAll($Tasks, $GraceMilliseconds)
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
        [ValidateRange(256, 65536)]
        [int]$ReadBufferSize = 4096,
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
    $processStarted = $false
    $terminationAttempted = $false

    try {
        if (-not $process.Start()) { throw "Unable to start $Name phase." }
        $processStarted = $true
        $standardOutputTask = $capture.ReadAsync($process.StandardOutput, $false)
        $standardErrorTask = $capture.ReadAsync($process.StandardError, $true)
        $streamTasks = @($standardOutputTask, $standardErrorTask)

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
                [void](Wait-SelfTestPhaseStreams -Tasks $streamTasks -GraceMilliseconds $TerminationGraceMilliseconds)
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

        [void](Wait-SelfTestPhaseStreams -Tasks $streamTasks -GraceMilliseconds $StreamDrainTimeoutMilliseconds)
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
        try { $process.StandardOutput.Close() } catch {}
        try { $process.StandardError.Close() } catch {}
        $process.Dispose()
    }
}

Export-ModuleMember -Function Add-SelfTestMarkerText, Invoke-SelfTestPhase
