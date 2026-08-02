[CmdletBinding()]
param(
    [string]$BuildDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
    $BuildDir = Join-Path $projectRoot 'build'
}

if (-not (Test-Path -LiteralPath $BuildDir -PathType Container)) {
    Write-Error "Build verification failed: build directory not found: $BuildDir"
    exit 1
}

$mapFiles = @(Get-ChildItem -LiteralPath $BuildDir -Recurse -File -Filter 'PlayerConsole.ino.map')
if ($mapFiles.Count -eq 0) {
    Write-Error "Build verification failed: no PlayerConsole.ino.map found under $BuildDir"
    exit 1
}

$sketchObjectPattern = 'objs\.a\(PlayerConsole\.ino\.cpp\.o\)'
$serial0Maps = @()
$missingHwCdcMaps = @()
foreach ($mapFile in $mapFiles) {
    $mapText = [System.IO.File]::ReadAllText($mapFile.FullName)
    if ($mapText -match "$sketchObjectPattern\s+\(Serial0\)") {
        $serial0Maps += $mapFile.FullName
    }
    $sketchUsesHwCdc = $mapText -match "$sketchObjectPattern\s+\(HWCDC::"
    $hwCdcSerialLinked = $mapText -match '(?m)^\s*\.bss\.HWCDCSerial\s*$'
    if (-not $sketchUsesHwCdc -or -not $hwCdcSerialLinked) {
        $missingHwCdcMaps += $mapFile.FullName
    }
}

if ($serial0Maps.Count -gt 0) {
    Write-Error ("Build verification failed: PlayerConsole.ino.cpp.o references Serial0 instead of HWCDCSerial in: " +
                 ($serial0Maps -join ', '))
    exit 1
}
if ($missingHwCdcMaps.Count -gt 0) {
    Write-Error ("Build verification failed: PlayerConsole.ino.cpp.o does not use linked HWCDCSerial/HWCDC in: " +
                 ($missingHwCdcMaps -join ', '))
    exit 1
}

$optionsFiles = @(Get-ChildItem -LiteralPath $BuildDir -Recurse -File -Filter 'build.options.json')
if ($optionsFiles.Count -eq 0) {
    Write-Error "Build verification failed: no build.options.json found under $BuildDir"
    exit 1
}

foreach ($optionsFile in $optionsFiles) {
    $options = Get-Content -LiteralPath $optionsFile.FullName -Raw | ConvertFrom-Json
    $fqbn = [string]$options.fqbn
    $customProperties = [string]$options.customBuildProperties
    if ($fqbn -notmatch '(^|,)CDCOnBoot=cdc(,|$)' -or $fqbn -notmatch '(^|,)DebugLevel=error(,|$)') {
        Write-Error "Build verification failed: CDCOnBoot=cdc and DebugLevel=error are required in $($optionsFile.FullName)"
        exit 1
    }
    if ($customProperties -match '(^|,\s*)build\.extra_flags=') {
        Write-Error "Build verification failed: build.extra_flags is overridden in $($optionsFile.FullName)"
        exit 1
    }
    if ($customProperties -notmatch '(^|,\s*)compiler\.c\.extra_flags=' -or
        $customProperties -notmatch '(^|,\s*)compiler\.cpp\.extra_flags=') {
        Write-Error "Build verification failed: project C/C++ extra flags are missing in $($optionsFile.FullName)"
        exit 1
    }
}

Write-Output ("BUILD OUTPUT CHECK PASS: {0} map file(s) bind the sketch to linked HWCDCSerial/HWCDC; board build.extra_flags remain available." -f $mapFiles.Count)
