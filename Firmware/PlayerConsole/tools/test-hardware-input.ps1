[CmdletBinding()]
param(
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools-3311\hardware-input-host"
)
$ErrorActionPreference = 'Stop'
$player = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
# Exercise the exact command parser/helper bodies, without compiling the full
# renderer or inventing a second parser. Reject missing extraction anchors.
$sketch = (Get-Content -LiteralPath (Join-Path $player 'PlayerConsole.ino') -Raw -Encoding UTF8).Replace("`r`n", "`n")
$parts = @()
foreach ($markers in @(
    @('bool inputTraceEnabled = false;', "#if GRIDOPOLY_DISPLAY_DIAGNOSTIC`nvoid serviceDisplayDiagnostic"),
    @('void serviceInputTrace(', "#if GRIDOPOLY_PLAYER_TRANSPORT == GRIDOPOLY_TRANSPORT_WIFI_UDP`nWifiUdpPlayerTransport")
)) {
    $start = $sketch.IndexOf($markers[0], [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing command-test start anchor: $($markers[0])" }
    $end = $sketch.IndexOf($markers[1], $start, [StringComparison]::Ordinal)
    if ($end -le $start) { throw "Missing command-test end anchor: $($markers[1])" }
    $parts += $sketch.Substring($start, $end - $start)
}
[IO.File]::WriteAllText((Join-Path $OutputDir 'extracted-command-path.h'), ($parts -join "`n"), [Text.UTF8Encoding]::new($false))
$includes = @("$PSScriptRoot\hardware-input-tests\stubs", $player, $OutputDir,
    "$repo\Firmware\libraries\GridopolyProtocol\src", "$repo\Firmware\libraries\GridopolyCore\src")
$arguments = @('/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/D_CRT_SECURE_NO_WARNINGS','/DGRIDOPOLY_SELF_TEST=1')
$arguments += $includes | ForEach-Object { '/I"' + $_ + '"' }
$lines = @('@echo off', "call `"$VsDevCmd`" -arch=x86 >nul", "cd /d `"$OutputDir`"")
foreach ($test in @('input', 'commands')) {
    $source = Join-Path $PSScriptRoot "hardware-input-tests\$test.cpp"
    $lines += 'cl ' + ($arguments -join ' ') + ' "' + $source + '" /Fe:' + $test + '-tests.exe'
    $lines += @('if errorlevel 1 exit /b 1', "$test-tests.exe", 'if errorlevel 1 exit /b 1')
}
$launcher = Join-Path $OutputDir 'run.cmd'
[IO.File]::WriteAllLines($launcher, $lines, [Text.UTF8Encoding]::new($false))
& cmd /c $launcher
exit $LASTEXITCODE
