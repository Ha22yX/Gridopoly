[CmdletBinding()]
param(
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools-3311\native-avatar-preview"
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$source = Join-Path $PSScriptRoot 'test-avatar-preview-buffers.cpp'
$lines = @('@echo off', "call `"$VsDevCmd`" -arch=x64 >nul", "cd /d `"$OutputDir`"",
    "cl /nologo /std:c++17 /EHsc /utf-8 /W4 /I`"$project`" `"$source`" /Fe:avatar-preview-tests.exe",
    'if errorlevel 1 exit /b 1', 'avatar-preview-tests.exe')
$script = Join-Path $OutputDir 'build.cmd'
[IO.File]::WriteAllLines($script, $lines, [Text.UTF8Encoding]::new($false))
& cmd /c $script
exit $LASTEXITCODE
