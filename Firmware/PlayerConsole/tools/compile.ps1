[CmdletBinding()]
param([switch]$SelfTest)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDir = Join-Path $projectRoot 'build'
$verifyScript = Join-Path $PSScriptRoot 'verify-build-output.ps1'
$glyphVerifyScript = Join-Path $PSScriptRoot 'verify-ui-glyphs.py'
$layoutVerifyScript = Join-Path $PSScriptRoot 'verify-ui-layout.py'
$fqbn = 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=cdc,DebugLevel=error,UploadMode=default,USBMode=hwcdc'
$flags = '-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DLV_COLOR_16_SWAP=0 -DESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED=1 -DBOARD_VIEWE_UEDX48480021_MD80ET -DCONFIG_LVGL_PORT_AVOID_TEARING_MODE=3'
if ($SelfTest) { $flags += ' -DGRIDOPOLY_SELF_TEST=1' }

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

& $arduinoCli compile --fqbn $fqbn --output-dir $buildDir `
    --build-property "compiler.c.extra_flags=$flags" `
    --build-property "compiler.cpp.extra_flags=$flags" `
    $projectRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& powershell -ExecutionPolicy Bypass -File $verifyScript -BuildDir $buildDir
exit $LASTEXITCODE
