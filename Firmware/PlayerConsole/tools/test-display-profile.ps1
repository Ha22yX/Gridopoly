[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'resolve-display-profile.ps1')
$checked = 0
foreach ($clock in @('vendor','pclk12')) {
    foreach ($diagnostic in @('none','counters')) {
        $profile = Resolve-PlayerDisplayProfile -DisplayClock $clock -DisplayDiagnostic $diagnostic
        $expectedHz = if ($clock -eq 'vendor') { 16000000 } else { 12000000 }
        if ($profile.requestedPclkHz -ne $expectedHz -or
            $profile.diagnosticOnly -ne ($diagnostic -eq 'counters') -or
            ($profile.extraFlags -match 'DISPLAY_PCLK_HZ') -ne ($clock -eq 'pclk12') -or
            ($profile.extraFlags -match 'DISPLAY_DIAGNOSTIC') -ne ($diagnostic -eq 'counters')) {
            throw "Profile mismatch: $clock / $diagnostic"
        }
        ++$checked
    }
}
# Existing default profiles keep their flags and diagnostic eligibility.
foreach ($mode in @('production','selftest','espnow')) {
    foreach ($battery in @('none','viewe-md80et-v10')) {
        $profile = Resolve-PlayerDisplayProfile -Mode $mode -BatteryBoard $battery
        if ($profile.extraFlags -or $profile.diagnosticOnly -or $profile.requestedPclkHz -ne 16000000) {
            throw "Default changed: $mode / $battery"
        }
        ++$checked
    }
}
foreach ($options in @(
    @{Mode='selftest'; DisplayClock='pclk12'},
    @{Mode='espnow'; DisplayClock='pclk12'},
    @{BatteryBoard='viewe-md80et-v10'; DisplayClock='pclk12'},
    @{Mode='selftest'; DisplayDiagnostic='counters'},
    @{Mode='espnow'; DisplayDiagnostic='counters'},
    @{BatteryBoard='viewe-md80et-v10'; DisplayDiagnostic='counters'},
    @{DisplayClock='unsupported'},
    @{DisplayDiagnostic='unsupported'}
)) {
    $rejected = $false
    try { Resolve-PlayerDisplayProfile @options | Out-Null } catch { $rejected = $true }
    if (!$rejected) { throw 'Unsupported profile was accepted' }
    ++$checked
}
Write-Host "DISPLAY PROFILE $checked/$checked PASS"
