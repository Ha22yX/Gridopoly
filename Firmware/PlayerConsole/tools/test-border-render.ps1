[CmdletBinding()]
param(
    [switch]$Profile,
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$CMake = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    [string]$OutputDir = (Join-Path $env:LOCALAPPDATA ('GridopolyPlayerTools-3311\pixel-tests\' + [guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
if (Test-Path -LiteralPath $OutputDir) { throw 'Use a new output directory for an isolated pixel test.' }
foreach ($tool in @($VsDevCmd, $CMake)) {
    if (!(Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Missing build tool: $tool" }
}
# MSVC's librarian can misread Unicode response-file object paths. Compile the
# unmodified library/config snapshot in an ASCII output directory.
if ($OutputDir -match '[^\x00-\x7F]') { throw 'Select an ASCII output directory for MSVC.' }
$library = Join-Path $repo 'Firmware\libraries\lvgl'
$config = Join-Path $repo 'Firmware\PlayerConsole\lv_conf.h'
New-Item -ItemType Directory -Path (Join-Path $OutputDir 'library'),(Join-Path $OutputDir 'config') | Out-Null
Copy-Item -LiteralPath (Join-Path $library 'src') -Destination (Join-Path $OutputDir 'library') -Recurse
Copy-Item -LiteralPath (Join-Path $library 'lvgl.h') -Destination (Join-Path $OutputDir 'library')
Copy-Item -LiteralPath $config -Destination (Join-Path $OutputDir 'config')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'test-border-render.c') -Destination (Join-Path $OutputDir 'render_compare.c')
$rectPath = Join-Path $OutputDir 'library\src\draw\sw\lv_draw_sw_rect.c'
$rect = [IO.File]::ReadAllText($rectPath)
# Generate the reference renderer from exactly the same source, removing only
# this optimization. Both implementations execute real LVGL mask/blend code.
$pattern = '(?s)    /\* A local refresh wholly inside the border''s hole cannot paint pixels\..*?_lv_area_is_in\(&draw_area, &safe_inner, LV_MAX\(rin - 2, 0\)\)\) return;\r?\n\r?\n'
$matches = [regex]::Matches($rect, $pattern)
if ($matches.Count -ne 1) { throw 'Expected exactly one border interior optimization block.' }
$reference = [regex]::Replace($rect, $pattern, '')
$backgroundPattern = '(?s)    /\* A clipped solid background wholly inside the rounded fill needs no mask\..*?\n    }\r?\n\r?\n'
if ([regex]::Matches($reference, $backgroundPattern).Count -ne 1) { throw 'Expected exactly one background interior optimization block.' }
$reference = [regex]::Replace($reference, $backgroundPattern, '')
$outerPattern = '(?s)    /\* A clip entirely in one outer corner can miss the rounded border\..*?\n    }\r?\n\r?\n'
if ([regex]::Matches($reference, $outerPattern).Count -ne 1) { throw 'Expected exactly one outer-corner optimization block.' }
$reference = [regex]::Replace($reference, $outerPattern, '')
$reference = $reference.Replace('#define ROW_CLIP_ENABLED profile_row_clip', '#define ROW_CLIP_ENABLED 0').Replace('#define ROW_CLIP_ENABLED 1', '#define ROW_CLIP_ENABLED 0')
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $OutputDir 'baseline_rect.c'), $reference, $utf8)
$cmakeText = @'
cmake_minimum_required(VERSION 3.20)
project(gridopoly_render C)
set(CMAKE_C_STANDARD 11)
file(GLOB_RECURSE LVGL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/library/src/*.c")
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC library config)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE _CRT_SECURE_NO_WARNINGS)
target_compile_options(lvgl PRIVATE /w)
add_library(baseline OBJECT baseline_rect.c)
target_include_directories(baseline PRIVATE library/src/draw/sw)
target_link_libraries(baseline PRIVATE lvgl)
target_compile_definitions(baseline PRIVATE lv_draw_sw_rect=baseline_lv_draw_sw_rect lv_draw_sw_bg=baseline_lv_draw_sw_bg draw_border_generic=baseline_draw_border_generic gridopoly_set_row_clip=baseline_gridopoly_set_row_clip)
add_executable(render_compare render_compare.c $<TARGET_OBJECTS:baseline>)
target_link_libraries(render_compare PRIVATE lvgl)
'@
if ($Profile) { $cmakeText += "`nadd_compile_definitions(GRIDOPOLY_SELF_TEST=1)`n" }
[IO.File]::WriteAllText((Join-Path $OutputDir 'CMakeLists.txt'), $cmakeText, $utf8)
@{
    source = $rectPath; sourceSha256 = (Get-FileHash -LiteralPath $rectPath).Hash
    referenceSha256 = (Get-FileHash -LiteralPath (Join-Path $OutputDir 'baseline_rect.c')).Hash
    configSha256 = (Get-FileHash -LiteralPath $config).Hash
    reference = 'Same renderer with three conservative culls removed and ROW_CLIP_ENABLED=0 (SPLIT_LIMIT stays 50)'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDir 'inputs.json') -Encoding utf8
$build = Join-Path $OutputDir 'build'
$lines = @('@echo off', "call `"$VsDevCmd`" -arch=x64 >nul",
    "`"$CMake`" -S `"$OutputDir`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release",
    'if errorlevel 1 exit /b 1', "`"$CMake`" --build `"$build`" -j 8",
    'if errorlevel 1 exit /b 1', "`"$(Join-Path $build 'render_compare.exe')`"")
$script = Join-Path $OutputDir 'build.cmd'
[IO.File]::WriteAllLines($script, $lines, $utf8)
Write-Host "Pixel test output: $OutputDir"
& cmd /c $script
exit $LASTEXITCODE
