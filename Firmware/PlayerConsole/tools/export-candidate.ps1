[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RunDir)
$ErrorActionPreference = 'Stop'
$RunDir = (Resolve-Path -LiteralPath $RunDir).Path
$inputs = Get-Content -LiteralPath (Join-Path $RunDir 'inputs.json') -Raw | ConvertFrom-Json
if ($inputs.mode -ne 'production' -or $inputs.flags -match 'GRIDOPOLY_SELF_TEST') {
    throw 'Only a normal production build can become a player release candidate'
}
$manifest = Get-Content -LiteralPath $inputs.sourceManifest -Raw | ConvertFrom-Json
foreach ($entry in $manifest.files) {
    if ((Get-FileHash -LiteralPath $entry.snapshotPath).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Frozen source changed during build: $($entry.path)"
    }
}
$flashArgs = Get-Content -LiteralPath (Join-Path $inputs.build 'flash_args') -Raw
if ($flashArgs -notmatch '--flash-mode\s+(\w+)\s+--flash-freq\s+(\w+)\s+--flash-size\s+(\w+)') {
    throw 'Cannot parse actual generated flash parameters'
}
$flashMode = $Matches[1]; $flashFrequency = $Matches[2]; $flashSize = $Matches[3]
$segments = @([regex]::Matches($flashArgs, '(?m)^\s*(0x[0-9a-fA-F]+)\s+(\S+)\s*$') | ForEach-Object {
    $binary = Join-Path $inputs.build $_.Groups[2].Value
    if (!(Test-Path -LiteralPath $binary -PathType Leaf)) { throw "Missing binary: $binary" }
    @{
        offset = [Convert]::ToUInt32($_.Groups[1].Value.Substring(2), 16)
        path = (Resolve-Path -LiteralPath $binary).Path
        sha256 = (Get-FileHash -LiteralPath $binary).Hash.ToLowerInvariant()
    }
})
if ($segments.Count -ne 4 -or @($segments | Where-Object { $_.offset -eq 65536 }).Count -ne 1) {
    throw 'Expected exactly bootloader, partition table, boot_app0 and application segments'
}
$candidatePath = Join-Path $RunDir 'candidate.json'
$displayDiagnostic = if ($inputs.displayDiagnostic) { [string]$inputs.displayDiagnostic } else { 'none' }
$displayClock = if ($inputs.displayClock) { [string]$inputs.displayClock } else { 'vendor' }
$batteryBoard = if ($inputs.batteryBoard) { [string]$inputs.batteryBoard } else { 'none' }
. (Join-Path $PSScriptRoot 'resolve-display-profile.ps1')
$displayProfile = Resolve-PlayerDisplayProfile -Mode $inputs.mode -BatteryBoard $batteryBoard -DisplayClock $displayClock -DisplayDiagnostic $displayDiagnostic
if ($inputs.requestedPclkHz -and $inputs.requestedPclkHz -ne $displayProfile.requestedPclkHz) {
    throw 'Requested pixel clock metadata does not match the selected build profile'
}
if (($inputs.flags -match '-DGRIDOPOLY_DISPLAY_DIAGNOSTIC=1(?:\s|$)') -ne $displayProfile.diagnosticOnly -or
    ($inputs.flags -match '-DGRIDOPOLY_DISPLAY_PCLK_HZ=12000000(?:\s|$)') -ne ($displayClock -eq 'pclk12')) {
    throw 'Display profile metadata does not match the actual compiler flags'
}
@{
    schemaVersion = 1; profile = 'player'; normalFirmware = $true; chip = 'esp32s3'
    flashMode = $flashMode; flashFrequency = $flashFrequency; flashSize = $flashSize
    gitRevision = $inputs.gitRevision; sourceManifest = $inputs.sourceManifest
    displayDiagnostic = $displayDiagnostic; diagnosticOnly = ($displayDiagnostic -ne 'none')
    displayClock = $displayClock; requestedPclkHz = $displayProfile.requestedPclkHz
    batteryBoard = $batteryBoard
    sourceDigest = (Get-FileHash -LiteralPath $inputs.sourceManifest).Hash.ToLowerInvariant()
    segments = $segments
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $candidatePath -Encoding utf8
Write-Host "Normal player candidate: $candidatePath"
