[CmdletBinding()]
param(
    [string]$VsRoot = 'D:\Program Files\Microsoft Visual Studio\18\Community',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools-3311\native-avatar-ui",
    [Parameter(Mandatory=$true)][string]$PreviewFile
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$stage=Join-Path $OutputDir 'source'; $build=Join-Path $OutputDir 'build'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
foreach ($part in @('Firmware/PlayerConsole','Firmware/libraries/lvgl','Firmware/libraries/GridopolyCore','Firmware/libraries/GridopolyProtocol')) {
    & robocopy (Join-Path $repo $part) (Join-Path $stage $part) /E /XF secrets.local.h /XD __pycache__ /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Source copy failed: $part" }
}
Copy-Item -LiteralPath $PreviewFile -Destination (Join-Path $OutputDir 'avatar-preview.rgb565') -Force
$cmake=Join-Path $VsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja=Join-Path $VsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$vsDev=Join-Path $VsRoot 'Common7/Tools/VsDevCmd.bat'
$source=Join-Path $stage 'Firmware/PlayerConsole/tools/avatar-ui-tests'
$lines=@('@echo off','chcp 65001 >nul',"call `"$vsDev`" -arch=x86 >nul", "cd /d `"$OutputDir`"",
    "`"$cmake`" -S `"$source`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=`"$ninja`"",
    'if not "%errorlevel%"=="0" exit /b %errorlevel%',"`"$cmake`" --build `"$build`" -j 8",
    'if not "%errorlevel%"=="0" exit /b %errorlevel%',"`"$build\avatar-ui.exe`"", 'exit /b %errorlevel%')
$launcher=Join-Path $OutputDir 'run.cmd'
[IO.File]::WriteAllLines($launcher,$lines,[Text.UTF8Encoding]::new($false))
& cmd /c $launcher
exit $LASTEXITCODE
