[CmdletBinding()]
param(
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools-3311\native-artwork-cache"
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$stage = Join-Path $OutputDir 'source'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
foreach ($part in @('Firmware/PlayerConsole','Firmware/libraries/GridopolyCore')) {
    & robocopy (Join-Path $repo $part) (Join-Path $stage $part) /E /XF secrets.local.h /XD __pycache__ /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Copy failed: $part" }
}
foreach ($pair in @(@('Assets/GridCity/StreetV3/device-rgb565','legacy'),
                    @('Assets/GridCity/DevelopmentV1/rgb565-player','development'))) {
    $dest = Join-Path $OutputDir "fixtures/$($pair[1])"
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    Copy-Item -Path (Join-Path $repo "$($pair[0])/*.rgb565") -Destination $dest -Force
}
$player = Join-Path $stage 'Firmware/PlayerConsole'
$core = Join-Path $stage 'Firmware/libraries/GridopolyCore/src'
$stubs = Join-Path $player 'tools/artwork-cache-tests/stubs'
$source = Join-Path $player 'tools/artwork-cache-tests/cache.cpp'
$fixture = Join-Path $OutputDir 'fixtures'
$compile = 'cl /nologo /std:c++17 /EHsc /utf-8 /W4 /I"{0}" /I"{1}" /DGRIDOPOLY_ARTWORK_HOST=\"configured-pi:8080\" "{2}" /Fe:artwork-cache-tests.exe' -f $stubs,$core,$source
$lines = @('@echo off', "call `"$VsDevCmd`" -arch=x86 >nul", "cd /d `"$OutputDir`"",
    $compile, 'if errorlevel 1 exit /b 1', "artwork-cache-tests.exe `"$fixture`"")
$launcher = Join-Path $OutputDir 'run.cmd'
[IO.File]::WriteAllLines($launcher, $lines, [Text.UTF8Encoding]::new($false))
& cmd /c $launcher
exit $LASTEXITCODE
