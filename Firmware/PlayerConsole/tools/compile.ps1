[CmdletBinding()]
param(
    [switch]$SelfTest,
    [switch]$EspNowFallback
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repositoryRoot = (Resolve-Path (Join-Path $projectRoot '..\..')).Path
$sharedLibraries = Join-Path $repositoryRoot 'Firmware\libraries'
$buildDir = Join-Path $projectRoot 'build'
$verifyScript = Join-Path $PSScriptRoot 'verify-build-output.ps1'
$glyphVerifyScript = Join-Path $PSScriptRoot 'verify-ui-glyphs.py'
$layoutVerifyScript = Join-Path $PSScriptRoot 'verify-ui-layout.py'
$tileAssetVerifyScript = Join-Path $PSScriptRoot 'verify-tile-assets.py'
$fqbn = 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=cdc,DebugLevel=error,UploadMode=default,USBMode=hwcdc'
# Keep the vendor-recommended direct anti-tearing mode. The RGB scan-line drift
# mitigation is configured separately through the larger DMA bounce buffer.
$flags = '-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DLV_COLOR_16_SWAP=0 -DESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED=1 -DBOARD_VIEWE_UEDX48480021_MD80ET -DCONFIG_LVGL_PORT_AVOID_TEARING_MODE=3'
if ($SelfTest) { $flags += ' -DGRIDOPOLY_SELF_TEST=1' }
if ($EspNowFallback) { $flags += ' -DGRIDOPOLY_USE_ESPNOW=1' }

if (Test-Path -LiteralPath $buildDir) { Remove-Item -LiteralPath $buildDir -Recurse -Force }
$arduinoCli = (Get-Command arduino-cli -ErrorAction SilentlyContinue).Source
if (-not $arduinoCli) {
    $arduinoCli = Join-Path $env:LOCALAPPDATA 'Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe'
}
if (-not (Test-Path -LiteralPath $arduinoCli)) { throw 'arduino-cli was not found.' }

$python = 'C:\Users\Administrator\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
if (-not (Test-Path -LiteralPath $python)) { $python = (Get-Command python -ErrorAction Stop).Source }
& $python $glyphVerifyScript
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $python $layoutVerifyScript
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $python $tileAssetVerifyScript
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $arduinoCli compile --clean --fqbn $fqbn --output-dir $buildDir `
    --libraries $sharedLibraries `
    --build-property "compiler.c.extra_flags=$flags" `
    --build-property "compiler.cpp.extra_flags=$flags" `
    $projectRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& powershell -ExecutionPolicy Bypass -File $verifyScript -BuildDir $buildDir
exit $LASTEXITCODE
