[CmdletBinding()]
param(
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools\native-join-transport"
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$code = @'
#include <array>
#include <Arduino.h>
#include <WiFiUdp.h>
#include <gridopoly/protocol/UdpEnvelope.h>
#define private public
#define protected public
#include "wifi_udp_player_transport.h"
#undef private
#undef protected
using namespace gridopoly::protocol;
'@
$code += "`n"
foreach ($item in @(
    @{file='espnow_player_transport.cpp'; class='EspNowPlayerTransport'; methods=@('push','poll','clearProjection')},
    @{file='wifi_udp_player_transport.cpp'; class='WifiUdpPlayerTransport'; methods=@('resetUdpSession','processUdpPairAccept','processDatagram')}
)) {
    $source = Get-Content -LiteralPath (Join-Path $repo ('Firmware/PlayerConsole/' + $item.file)) -Raw
    foreach ($method in $item.methods) {
        $match = [regex]::Match($source, "(?ms)^(?:bool|void) $($item.class)::$method\(.*?^\}")
        if (!$match.Success) { throw "Cannot extract production method: $method" }
        $code += $match.Value + "`n"
    }
}
$productionPath = Join-Path $OutputDir 'production-admission.cpp'
[IO.File]::WriteAllText($productionPath, $code, [Text.UTF8Encoding]::new($false))
$sources = @('Firmware/PlayerConsole/tools/test-join-transport.cpp',
    'Firmware/PlayerConsole/app_state.cpp', 'Firmware/PlayerConsole/demo_data.cpp',
    'Firmware/PlayerConsole/grid_city_visual_catalog.cpp',
    'Firmware/libraries/GridopolyCore/src/gridopoly/core/BoardCatalog.cpp',
    'Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.cpp',
    'Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/UdpEnvelope.cpp')
$includes = @('Firmware/PlayerConsole/tools/host-stubs', 'Firmware/PlayerConsole',
    'Firmware/libraries/GridopolyCore/src', 'Firmware/libraries/GridopolyProtocol/src')
$arguments = @('/nologo','/std:c++17','/EHsc','/utf-8','/W4','/D_CRT_SECURE_NO_WARNINGS')
$arguments += $includes | ForEach-Object { '/I"' + (Join-Path $repo $_) + '"' }
$arguments += $sources | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }
$arguments += '"' + $productionPath + '"'
$arguments += '/Fe:gridopoly-admission-check.exe'
$lines = @('@echo off', 'chcp 65001 >nul', "call `"$VsDevCmd`" -arch=x86 >nul", "cd /d `"$OutputDir`"",
    ('cl ' + ($arguments -join ' ')), 'if errorlevel 1 exit /b 1', 'gridopoly-admission-check.exe')
$script = Join-Path $OutputDir 'build.cmd'
[IO.File]::WriteAllLines($script, $lines, [Text.UTF8Encoding]::new($false))
& cmd /c $script
exit $LASTEXITCODE
