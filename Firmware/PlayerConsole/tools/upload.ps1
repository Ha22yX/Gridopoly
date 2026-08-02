[CmdletBinding()]
param([string]$Port = '')

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDir = Join-Path $projectRoot 'build'
$fqbn = 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=cdc,DebugLevel=error,UploadMode=default,USBMode=hwcdc'
$arduinoCli = (Get-Command arduino-cli -ErrorAction SilentlyContinue).Source
if (-not $arduinoCli) { $arduinoCli = Join-Path $env:LOCALAPPDATA 'Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe' }
if (-not (Test-Path -LiteralPath $arduinoCli)) { throw 'arduino-cli was not found.' }

if (-not $Port) {
    $ports = & $arduinoCli board list --format json | ConvertFrom-Json
    $candidate = $ports.detected_ports | Where-Object { $_.port.address -like 'COM*' } | Select-Object -First 1
    if (-not $candidate) { throw 'No ESP32 serial port was detected.' }
    $Port = $candidate.port.address
}
Write-Host "Uploading Gridopoly Player Console to $Port"
& $arduinoCli upload --fqbn $fqbn --port $Port --input-dir $buildDir
exit $LASTEXITCODE
