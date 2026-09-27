[CmdletBinding()]
param(
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools-3311\battery-interface-20260927\host"
)
$ErrorActionPreference='Stop'
$player=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$sources=@("$PSScriptRoot\test-battery-telemetry.cpp", "$player\battery_telemetry.cpp", "$player\battery_hardware.cpp")
$arguments=@('/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/D_CRT_SECURE_NO_WARNINGS',('/I"'+$player+'"'))
$arguments += $sources | ForEach-Object { '"'+$_+'"' }
$arguments += '/Fe:battery-tests.exe'
$lines=@('@echo off', "call `"$VsDevCmd`" -arch=x86 >nul", "cd /d `"$OutputDir`"",
    ('cl '+($arguments -join ' ')), 'if errorlevel 1 exit /b 1', 'battery-tests.exe')
$launcher=Join-Path $OutputDir 'run.cmd'
[IO.File]::WriteAllLines($launcher,$lines,[Text.UTF8Encoding]::new($false))
& cmd /c $launcher
exit $LASTEXITCODE
