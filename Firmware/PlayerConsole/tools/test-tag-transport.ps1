[CmdletBinding()]
param(
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools\native-tag-transport"
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$production = Get-Content -LiteralPath (Join-Path $repo 'Firmware/PlayerConsole/espnow_player_transport.cpp') -Raw
$queue = '#include "espnow_player_transport.h"' + "`n"
foreach ($method in @('push','poll')) {
    $match = [regex]::Match($production, "(?ms)^bool EspNowPlayerTransport::$method\([^\r\n]*\)\s*\r?\n\{.*?^\}")
    if (!$match.Success) { throw "Cannot extract production queue method: $method" }
    $queue += $match.Value + "`n"
}
$queuePath = Join-Path $OutputDir 'production-queue.cpp'
[IO.File]::WriteAllText($queuePath, $queue, [Text.UTF8Encoding]::new($false))
$sources = @('Firmware/PlayerConsole/tools/test-tag-transport.cpp',
    'Firmware/PlayerConsole/tag_pair_transport.cpp',
    'Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.cpp')
$includes = @('Firmware/PlayerConsole/tools/host-stubs', 'Firmware/PlayerConsole',
    'Firmware/libraries/GridopolyProtocol/src')
$arguments = @('/nologo','/std:c++17','/EHsc','/utf-8','/W4','/D_CRT_SECURE_NO_WARNINGS')
$arguments += $includes | ForEach-Object { '/I"' + (Join-Path $repo $_) + '"' }
$arguments += $sources | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }
$arguments += '"' + $queuePath + '"'
$arguments += '/Fe:gridopoly-tag-check.exe'
$lines = @('@echo off', 'chcp 65001 >nul', "call `"$VsDevCmd`" -arch=x86 >nul", "cd /d `"$OutputDir`"",
    ('cl ' + ($arguments -join ' ')), 'if errorlevel 1 exit /b 1', 'gridopoly-tag-check.exe')
$script = Join-Path $OutputDir 'build.cmd'
[IO.File]::WriteAllLines($script, $lines, [Text.UTF8Encoding]::new($false))
& cmd /c $script
exit $LASTEXITCODE
