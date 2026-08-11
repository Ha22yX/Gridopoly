[CmdletBinding()]
param(
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port = 'COM5',
  [string]$BuildDir = ''
)

. (Join-Path $PSScriptRoot 'Common.ps1')
Assert-GridopolyTooling
Assert-GridopolyPort -Port $Port
$resolvedBuildDir = Resolve-GridopolyBuildDir -BuildDir $BuildDir
& (Join-Path $PSScriptRoot 'verify-build.ps1') -BuildDir $resolvedBuildDir

Write-Host "GRIDOPOLY_UPLOAD port=$Port"
& $script:GridopolyArduinoCli upload `
  --fqbn $script:GridopolyFqbn `
  --port $Port `
  --input-dir $resolvedBuildDir `
  $script:GridopolySketchDir
if ($LASTEXITCODE -ne 0) {
  throw "Arduino upload failed with exit code $LASTEXITCODE."
}
Write-Host "GRIDOPOLY_UPLOAD_OK port=$Port"
