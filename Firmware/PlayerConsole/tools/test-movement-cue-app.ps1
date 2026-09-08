[CmdletBinding()]
param(
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools\native"
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$sources = @('Firmware/PlayerConsole/tools/test-movement-cue-app.cpp',
    'Firmware/PlayerConsole/app_state.cpp', 'Firmware/PlayerConsole/demo_data.cpp',
    'Firmware/PlayerConsole/grid_city_visual_catalog.cpp',
    'Firmware/libraries/GridopolyCore/src/gridopoly/core/BoardCatalog.cpp',
    'Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.cpp')
$includes = @('Firmware/PlayerConsole/tools/host-stubs', 'Firmware/PlayerConsole',
    'Firmware/libraries/GridopolyCore/src','Firmware/libraries/GridopolyProtocol/src')
$arguments = @('/nologo','/std:c++17','/EHsc','/utf-8','/W4','/D_CRT_SECURE_NO_WARNINGS')
$arguments += $includes | ForEach-Object { '/I"' + (Join-Path $repo $_) + '"' }
$arguments += $sources | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }
$arguments += '/Fe:cue-app-tests.exe'
# x86 preserves the ESP32's four-byte pointers for the existing DTO size gate.
$lines = @('@echo off', "call `"$VsDevCmd`" -arch=x86 >nul", "cd /d `"$OutputDir`"",
    ('cl ' + ($arguments -join ' ')), 'if errorlevel 1 exit /b 1', 'cue-app-tests.exe')
$script = Join-Path $OutputDir 'build.cmd'
[IO.File]::WriteAllLines($script, $lines, [Text.UTF8Encoding]::new($false))
& cmd /c $script
exit $LASTEXITCODE
