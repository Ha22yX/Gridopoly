[CmdletBinding()]
param(
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools-3311\handwriting-com28-20261016\host-trace"
)
$ErrorActionPreference='Stop'
$player=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$arguments=@('/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/D_CRT_SECURE_NO_WARNINGS',
    ('/I"'+$PSScriptRoot+'\touch-trace-tests\stubs"'),('/I"'+$player+'"'),
    ('"'+$PSScriptRoot+'\touch-trace-tests\trace.cpp"'),('"'+$player+'\touch_trace.cpp"'),'/Fe:touch-trace.exe')
$lines=@('@echo off',"call `"$VsDevCmd`" -arch=x86 >nul", "cd /d `"$OutputDir`"",('cl '+($arguments -join ' ')),
    'if errorlevel 1 exit /b 1','touch-trace.exe')
$launcher=Join-Path $OutputDir 'run.cmd'
[IO.File]::WriteAllLines($launcher,$lines,[Text.UTF8Encoding]::new($false))
& cmd /c $launcher
exit $LASTEXITCODE
