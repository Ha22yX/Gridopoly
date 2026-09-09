[CmdletBinding()]
param(
    [ValidateSet('production','selftest','espnow')][string]$Mode = 'production',
    [string]$ToolRoot = "$env:LOCALAPPDATA\GridopolyPlayerTools-3311",
    [switch]$ReuseBuildCache
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repo = (Resolve-Path (Join-Path $project '..\..')).Path
$ToolRoot = (Resolve-Path -LiteralPath $ToolRoot).Path
$cli = Join-Path $ToolRoot 'cli\arduino-cli.exe'
$config = Join-Path $ToolRoot 'arduino-cli.yaml'
$verifier = Join-Path $PSScriptRoot 'verify-build-output.ps1'
# Project libraries are freshly snapshotted from this checkout for each run.
# Third-party board/IO libraries come from the CLI config's user/libraries path.
$libraries = Join-Path $repo 'Firmware\libraries'
foreach ($required in @($cli, $config, $verifier, (Join-Path $project 'PlayerConsole.ino')) +
    @('GridopolyCore','GridopolyProtocol','lvgl' | ForEach-Object {
        Join-Path $libraries "$_\library.properties"
    })) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing build prerequisite: $required" }
}
$configJson = & $cli config dump --format json --config-file $config
if ($LASTEXITCODE -ne 0) { throw 'Arduino CLI could not read its config' }
$directories = ($configJson | ConvertFrom-Json).config.directories
foreach ($directory in @($directories.data, $directories.user)) {
    if (!$directory -or !(Test-Path -LiteralPath $directory -PathType Container)) {
        throw "Invalid Arduino CLI directory: $directory"
    }
}
$coreJson = & $cli core list --format json --config-file $config
if ($LASTEXITCODE -ne 0) { throw 'Arduino CLI could not list installed cores' }
$core = @((($coreJson | ConvertFrom-Json).platforms) | Where-Object { $_.id -eq 'esp32:esp32' })
if ($core.Count -ne 1 -or $core[0].installed_version -ne '3.3.11') {
    throw 'This build is validated with esp32:esp32 3.3.11; select a matching ToolRoot/config'
}
$externalLibraries = Join-Path $directories.user 'libraries'
$dependencyVersions = @{}
foreach ($name in @('ESP32_Display_Panel','ESP32_IO_Expander','esp-lib-utils','ESP32_Button','ESP32_Knob')) {
    $properties = Join-Path $externalLibraries "$name\library.properties"
    if (!(Test-Path -LiteralPath $properties -PathType Leaf)) { throw "Missing external library: $properties" }
    $dependencyVersions[$name] = @(Get-Content -LiteralPath $properties | Where-Object { $_ -match '^version=' })
}
# Never overlay or delete an old source/output directory. Each invocation owns
# a new snapshot; renamed/deleted sketch files cannot leak into later builds.
$runName = '{0}-{1}-{2}' -f $Mode, (Get-Date -Format 'yyyyMMdd-HHmmss'), [guid]::NewGuid().ToString('N')
$run = Join-Path $ToolRoot "runs\$runName"
$stage = Join-Path $run 'PlayerConsole'
New-Item -ItemType Directory -Path $stage | Out-Null
Get-ChildItem -LiteralPath $project | Where-Object { $_.Name -notin @('build','tools') } |
    Copy-Item -Destination $stage -Recurse -Force
# Keep network-checkout headers local during dependency scans and prevent a
# later shared-library edit from changing an already running build's inputs.
# Each run owns its library copy; no old ToolRoot mirror is reused.
$sourceLibraries = $libraries
$libraries = Join-Path $run 'libraries'
New-Item -ItemType Directory -Path $libraries | Out-Null
Get-ChildItem -LiteralPath $sourceLibraries | Copy-Item -Destination $libraries -Recurse
$flags = '-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DLV_COLOR_16_SWAP=0 -DESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED=1 -DBOARD_VIEWE_UEDX48480021_MD80ET -DCONFIG_LVGL_PORT_AVOID_TEARING_MODE=3'
$fqbn = 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=cdc,DebugLevel=error,UploadMode=default,USBMode=hwcdc'
if ($Mode -eq 'selftest') { $flags += ' -DGRIDOPOLY_SELF_TEST=1' }
if ($Mode -eq 'espnow') { $flags += ' -DGRIDOPOLY_USE_ESPNOW=1' }
# Arduino's dependency files can retain an older snapshot's lv_conf.h path.
# Reuse objects only when every sketch/library input and build flag is identical;
# a changed configuration/header/deleted file must select a different cache.
$fingerprintRows = [Collections.Generic.List[string]]::new()
$fingerprintRows.Add("esp32:esp32@3.3.11|$fqbn|$Mode|$flags|$config|$externalLibraries")
foreach ($name in ($dependencyVersions.Keys | Sort-Object)) {
    $fingerprintRows.Add("external:$name=$($dependencyVersions[$name] -join ',')")
}
foreach ($inputRoot in @(@{name='sketch'; path=$stage}, @{name='libraries'; path=$libraries})) {
    Get-ChildItem -LiteralPath $inputRoot.path -Recurse -File | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($inputRoot.path.Length + 1)
        $fingerprintRows.Add("$($inputRoot.name)/$relative=$((Get-FileHash -LiteralPath $_.FullName).Hash)")
    }
}
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $fingerprint = ([BitConverter]::ToString($sha.ComputeHash(
        [Text.Encoding]::UTF8.GetBytes(($fingerprintRows -join "`n"))))).Replace('-', '').ToLowerInvariant()
} finally { $sha.Dispose() }
$build = if ($ReuseBuildCache) { Join-Path $ToolRoot "build-$Mode-$fingerprint" } else { Join-Path $run 'build' }
$output = Join-Path $run 'output'
@{
    mode = $Mode; core = 'esp32:esp32@3.3.11'; fqbn = $fqbn; source = $project; snapshot = $stage
    sourceProjectLibraries = $sourceLibraries
    projectLibraries = $libraries; externalLibraries = $externalLibraries
    externalVersions = $dependencyVersions; config = $config; flags = $flags
    build = $build; output = $output; reuseBuildCache = [bool]$ReuseBuildCache
    buildInputFingerprint = $fingerprint
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $run 'inputs.json') -Encoding utf8
Write-Host "Build inputs: $(Join-Path $run 'inputs.json')"
Write-Host "Build output: $output"
$cacheLock = $null
try {
    if ($ReuseBuildCache) {
        # Only one writer can use this exact cache; different inputs are isolated.
        $cacheLock = [IO.File]::Open(($build + '.lock'),
            [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    }
    & $cli compile --fqbn $fqbn --jobs 8 --build-path $build --output-dir $output --libraries $libraries --config-file $config --build-property "compiler.c.extra_flags=$flags" --build-property "compiler.cpp.extra_flags=$flags" $stage
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $verifier -BuildDir $build
    exit $LASTEXITCODE
} finally {
    if ($null -ne $cacheLock) { $cacheLock.Dispose() }
}
