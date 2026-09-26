[CmdletBinding()]
param(
    [string]$VsRoot = 'D:\Program Files\Microsoft Visual Studio\18\Community',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools\native-input-runtime"
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
# MSVC response files need an ASCII source path on systems with a non-UTF8 ACP.
$stage = Join-Path $OutputDir 'source'
$build = Join-Path $OutputDir 'build'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
foreach ($part in @('Firmware/PlayerConsole', 'Firmware/libraries/lvgl',
                    'Firmware/libraries/GridopolyCore', 'Firmware/libraries/GridopolyProtocol')) {
    & robocopy (Join-Path $repo $part) (Join-Path $stage $part) /E /XF secrets.local.h /XD __pycache__ /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Source copy failed: $part" }
}
$cmake = Join-Path $VsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja = Join-Path $VsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$vsDev = Join-Path $VsRoot 'Common7/Tools/VsDevCmd.bat'
$testSource = Join-Path $stage 'Firmware/PlayerConsole/tools/input-runtime-tests'
$lines = @('@echo off', 'chcp 65001 >nul', "call `"$vsDev`" -arch=x86 >nul",
    "`"$cmake`" -S `"$testSource`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=`"$ninja`"",
    'if errorlevel 1 exit /b 1', "`"$cmake`" --build `"$build`" -j 8",
    'if errorlevel 1 exit /b 1', "`"$(Join-Path $build 'input-runtime.exe')`"")
$launcher = Join-Path $OutputDir 'run.cmd'
[IO.File]::WriteAllLines($launcher, $lines, [Text.UTF8Encoding]::new($false))
& cmd /c $launcher
exit $LASTEXITCODE
