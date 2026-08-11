[CmdletBinding()]
param(
  [string]$BuildDir = ''
)

. (Join-Path $PSScriptRoot 'Common.ps1')
Assert-GridopolyTooling
$resolvedBuildDir = Resolve-GridopolyBuildDir -BuildDir $BuildDir
New-Item -ItemType Directory -Force -Path $resolvedBuildDir | Out-Null

& node (Join-Path $PSScriptRoot 'test-web-ui-layout.mjs')
if ($LASTEXITCODE -ne 0) {
  throw "Web UI asset verification failed with exit code $LASTEXITCODE."
}

Write-Host 'GRIDOPOLY_COMPILE target=esp32s3 flash=16MB psram=8MB partition=custom'
& $script:GridopolyArduinoCli compile `
  --fqbn $script:GridopolyFqbn `
  --library $script:GridopolyCoreLibrary `
  --library $script:GridopolyProtocolLibrary `
  --build-path $resolvedBuildDir `
  --warnings all `
  $script:GridopolySketchDir
if ($LASTEXITCODE -ne 0) {
  throw "Arduino compilation failed with exit code $LASTEXITCODE."
}

Write-Host "GRIDOPOLY_COMPILE_OK build=$resolvedBuildDir"
