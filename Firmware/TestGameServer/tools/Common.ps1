Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:GridopolySketchDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$script:GridopolyRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$script:GridopolyLibrariesDir = Join-Path $script:GridopolyRepoRoot 'Firmware\libraries'
$script:GridopolyCoreLibrary = Join-Path $script:GridopolyLibrariesDir 'GridopolyCore'
$script:GridopolyProtocolLibrary = Join-Path $script:GridopolyLibrariesDir 'GridopolyProtocol'
$script:GridopolyArduinoCli = Join-Path $env:LOCALAPPDATA 'Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe'
$script:GridopolyFqbn = 'esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=custom,DebugLevel=error,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none'

function Assert-GridopolyTooling {
  if (-not (Test-Path -LiteralPath $script:GridopolyArduinoCli -PathType Leaf)) {
    throw "Arduino CLI was not found at the bundled Arduino IDE location."
  }
  if (-not (Test-Path -LiteralPath $script:GridopolyLibrariesDir -PathType Container)) {
    throw "Gridopoly Arduino libraries directory is missing."
  }
  if (-not (Test-Path -LiteralPath $script:GridopolyCoreLibrary -PathType Container) -or
      -not (Test-Path -LiteralPath $script:GridopolyProtocolLibrary -PathType Container)) {
    throw "Gridopoly core or protocol library is missing."
  }
}

function Resolve-GridopolyBuildDir {
  param([string]$BuildDir)
  if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $script:GridopolySketchDir 'build'
  }
  $full = [System.IO.Path]::GetFullPath($BuildDir)
  $allowedRoot = $script:GridopolySketchDir.TrimEnd('\') + '\'
  if (-not $full.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Build directory must remain inside Firmware\TestGameServer."
  }
  return $full
}

function Assert-GridopolyPort {
  param([Parameter(Mandatory = $true)][string]$Port)
  if ($Port -notmatch '^COM[0-9]+$') {
    throw "A concrete COM port such as COM5 is required."
  }
  $ports = [System.IO.Ports.SerialPort]::GetPortNames()
  if ($ports -notcontains $Port) {
    throw "Requested port $Port is not currently available."
  }
}
