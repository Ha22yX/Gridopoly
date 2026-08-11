[CmdletBinding()]
param(
  [string]$BuildDir = ''
)

. (Join-Path $PSScriptRoot 'Common.ps1')
$resolvedBuildDir = Resolve-GridopolyBuildDir -BuildDir $BuildDir
$required = @(
  'TestGameServer.ino.bin',
  'TestGameServer.ino.bootloader.bin',
  'TestGameServer.ino.partitions.bin',
  'TestGameServer.ino.elf'
)
foreach ($name in $required) {
  $path = Join-Path $resolvedBuildDir $name
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Missing build artifact: $name"
  }
}

$application = Get-Item -LiteralPath (Join-Path $resolvedBuildDir 'TestGameServer.ino.bin')
if ($application.Length -gt 0x400000) {
  throw "Application image exceeds the 4 MiB factory partition."
}

$partitionCsv = Get-Content -Raw -LiteralPath (Join-Path $script:GridopolySketchDir 'partitions.csv')
$expectedRows = @(
  '0x9000,   0x5000',
  '0xE000,   0x2000',
  '0x10000,  0x400000',
  '0x410000, 0xBE0000',
  '0xFF0000, 0x10000'
)
foreach ($row in $expectedRows) {
  if (-not $partitionCsv.Contains($row)) {
    throw "Partition layout mismatch near $row"
  }
}

Write-Host ("GRIDOPOLY_BUILD_OK appBytes={0} factoryBytes={1} flashBytes={2}" -f $application.Length, 0x400000, 0x1000000)
