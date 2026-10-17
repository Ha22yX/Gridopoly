[CmdletBinding()]
param(
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools-3311\battery-service-20261016\host-service"
)
$ErrorActionPreference='Stop'
$player=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$sources=@("$PSScriptRoot\battery-service-tests\service.cpp", "$player\battery_telemetry.cpp", "$player\battery_hardware.cpp")
$arguments=@('/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/D_CRT_SECURE_NO_WARNINGS',
    ('/I"'+$PSScriptRoot+'\battery-service-tests\stubs"'),('/I"'+$PSScriptRoot+'"'),('/I"'+$player+'"'))
$arguments += $sources | ForEach-Object { '"'+$_+'"' }
$lines=@('@echo off', "call `"$VsDevCmd`" -arch=x86 >nul", "cd /d `"$OutputDir`"",
    ('cl '+($arguments -join ' ')+' /DGRIDOPOLY_BATTERY_BOARD=1 /Fe:service-enabled.exe'), 'if errorlevel 1 exit /b 1')
foreach ($scenario in @('success','create-failure','uart-failure','buffer-failure')) {
    $lines += "service-enabled.exe $scenario"; $lines += 'if errorlevel 1 exit /b 1'
}
$lines += ('cl '+($arguments -join ' ')+' /DGRIDOPOLY_BATTERY_BOARD=0 /Fe:service-disabled.exe')
$lines += @('if errorlevel 1 exit /b 1', 'service-disabled.exe disabled')
$launcher=Join-Path $OutputDir 'run.cmd'
[IO.File]::WriteAllLines($launcher,$lines,[Text.UTF8Encoding]::new($false))
& cmd /c $launcher
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# Guard the actual render call site as well as the clock-order adapter regression.
$sketch=Get-Content -LiteralPath (Join-Path $player 'PlayerConsole.ino') -Raw
if ($sketch -notmatch '(?s)const auto batterySnapshot = batteryHardwareStatus\(0\);\s+const uint32_t batteryNowMs = millis\(\);[^\r\n]*\r?\n\s+uiBatteryRender\(batteryUi, batterySnapshot, batteryNowMs\);') {
    throw 'Battery render must sample the clock after acquiring the worker snapshot'
}
Write-Output '[PASS] production render call samples the clock after the worker snapshot'
