[CmdletBinding()]
param(
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port = 'COM5'
)

. (Join-Path $PSScriptRoot 'Common.ps1')
Assert-GridopolyPort -Port $Port
$esptool = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools\esptool_py\5.3.1\esptool.exe'
if (-not (Test-Path -LiteralPath $esptool -PathType Leaf)) {
  throw 'Bundled Espressif esptool was not found.'
}

Write-Host "GRIDOPOLY_PROBE port=$Port"
& $esptool --chip esp32s3 --port $Port flash-id
if ($LASTEXITCODE -ne 0) {
  throw "ESP32-S3 probe failed with exit code $LASTEXITCODE."
}
