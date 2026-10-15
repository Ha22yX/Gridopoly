[CmdletBinding()]
param(
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools\native-display-diagnostic"
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$source = (Resolve-Path (Join-Path $PSScriptRoot 'test-display-diagnostic.cpp')).Path
$lines = @('@echo off', "call `"$VsDevCmd`" -arch=x86 >nul",
    "cd /d `"$OutputDir`"",
    "cl /nologo /std:c++17 /EHsc /utf-8 /W4 `"$source`" /Fe:display-diagnostic-tests.exe",
    'if errorlevel 1 exit /b 1', 'display-diagnostic-tests.exe')
$launcher = Join-Path $OutputDir 'run.cmd'
[IO.File]::WriteAllLines($launcher, $lines, [Text.UTF8Encoding]::new($false))
& cmd /c $launcher
exit $LASTEXITCODE
