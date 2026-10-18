[CmdletBinding()]
param(
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools-3311\native-avatar-identity-v2"
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$OutputDir = (Resolve-Path $OutputDir).Path
$production = Get-Content -LiteralPath (Join-Path $repo 'Firmware/PlayerConsole/espnow_player_transport.cpp') -Raw
$code = @'
#include "espnow_player_transport.h"
#include <cstring>
using namespace gridopoly::protocol;
'@ + "`n"
foreach ($function in @('toWireRecipe', 'fromWireRecipe', 'mapIdentityStage',
        'EspNowPlayerTransport::push', 'EspNowPlayerTransport::poll',
        'EspNowPlayerTransport::processIdentitySnapshot', 'EspNowPlayerTransport::sendIdentityRequest')) {
    $match = [regex]::Match($production, "(?ms)^\w+ $([regex]::Escape($function))\(.*?^\}")
    if (!$match.Success) { throw "Cannot extract production function: $function" }
    $code += $match.Value + "`n"
}
$productionPath = Join-Path $OutputDir 'production-avatar-identity.cpp'
[IO.File]::WriteAllText($productionPath, $code, [Text.UTF8Encoding]::new($false))
$sources = @('Firmware/PlayerConsole/tools/test-avatar-identity.cpp',
    'Firmware/PlayerConsole/app_state.cpp', 'Firmware/PlayerConsole/demo_data.cpp',
    'Firmware/PlayerConsole/grid_city_visual_catalog.cpp',
    'Firmware/libraries/GridopolyCore/src/gridopoly/core/BoardCatalog.cpp',
    'Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.cpp')
$includes = @('Firmware/PlayerConsole/tools/host-stubs', 'Firmware/PlayerConsole',
    'Firmware/libraries/GridopolyCore/src', 'Firmware/libraries/GridopolyProtocol/src')
$arguments = @('/nologo','/std:c++17','/EHsc','/utf-8','/W4','/D_CRT_SECURE_NO_WARNINGS')
$arguments += $includes | ForEach-Object { '/I"' + (Join-Path $repo $_) + '"' }
$arguments += $sources | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }
$arguments += '"' + $productionPath + '"'
$lines = @('@echo off', 'chcp 65001 >nul', "call `"$VsDevCmd`" -arch=x86 >nul", "cd /d `"$OutputDir`"")
foreach ($version in 1, 2) {
    $lines += ('cl ' + ($arguments -join ' ') + " /DGRIDOPOLY_AVATAR_DEFAULT_CATALOG_VERSION=$version /Fe:avatar-identity-v$version-tests.exe")
    $lines += 'if not "%errorlevel%"=="0" exit /b %errorlevel%'
    $lines += "avatar-identity-v$version-tests.exe"
    $lines += 'if not "%errorlevel%"=="0" exit /b %errorlevel%'
}
$lines += 'exit /b 0'
$script = Join-Path $OutputDir 'build.cmd'
[IO.File]::WriteAllLines($script, $lines, [Text.UTF8Encoding]::new($false))
& cmd /c $script
exit $LASTEXITCODE
