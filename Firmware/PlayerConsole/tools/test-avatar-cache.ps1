[CmdletBinding()]
param(
    [string]$VsDevCmd = 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    [string]$OutputDir = "$env:LOCALAPPDATA\GridopolyPlayerTools-3311\native-avatar-cache-v2",
    [string]$V2Pack
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repo = (Resolve-Path (Join-Path $project '..\..')).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$OutputDir = (Resolve-Path $OutputDir).Path
$source = Join-Path $PSScriptRoot 'avatar-cache-tests\cache.cpp'
$stubs = Join-Path $PSScriptRoot 'avatar-cache-tests\stubs'
$pack = Join-Path $repo 'Assets\GridCity\Avatars\V1\runtime\components-v1'
$packArguments = "`"$pack`""
if ($V2Pack) { $packArguments += ' "' + (Resolve-Path $V2Pack).Path + '"' }
$lines = @('@echo off', 'chcp 65001 >nul', "call `"$VsDevCmd`" -arch=x86 >nul", "cd /d `"$OutputDir`"")
foreach ($version in 1, 2) {
    $lines += "cl /nologo /std:c++17 /EHsc /utf-8 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /DGRIDOPOLY_AVATAR_DEFAULT_CATALOG_VERSION=$version /I`"$stubs`" /I`"$project`" `"$source`" /Fe:avatar-cache-v$version-tests.exe"
    $lines += 'if not "%errorlevel%"=="0" exit /b %errorlevel%'
    $lines += "avatar-cache-v$version-tests.exe $packArguments"
    $lines += 'if not "%errorlevel%"=="0" exit /b %errorlevel%'
}
$lines += 'exit /b 0'
$script = Join-Path $OutputDir 'build.cmd'
[IO.File]::WriteAllLines($script, $lines, [Text.UTF8Encoding]::new($false))
& cmd /c $script
exit $LASTEXITCODE
