[CmdletBinding()]
param(
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port = 'COM5',
  [ValidateRange(5, 45)]
  [int]$Seconds = 25
)

. (Join-Path $PSScriptRoot 'Common.ps1')
Assert-GridopolyPort -Port $Port

$serial = [System.IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$serial.ReadTimeout = 250
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$markers = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
try {
  $serial.Open()
  $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      $line = $serial.ReadLine().Trim()
      if ($line.StartsWith('GRIDOPOLY_', [System.StringComparison]::Ordinal)) {
        Write-Host $line
        $marker = ($line -split '[ =]', 2)[0]
        [void]$markers.Add($marker)
      }
    } catch [System.TimeoutException] {
      # Keep listening until the bounded deadline.
    }
  }
} finally {
  if ($serial.IsOpen) { $serial.Close() }
  $serial.Dispose()
}

if (-not ($markers.Contains('GRIDOPOLY_READY') -or $markers.Contains('GRIDOPOLY_ALIVE'))) {
  throw 'Firmware readiness marker was not observed on the requested port.'
}
Write-Host "GRIDOPOLY_SMOKE_OK port=$Port"
