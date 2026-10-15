function Resolve-PlayerDisplayProfile {
    param(
        [ValidateSet('production','selftest','espnow')][string]$Mode = 'production',
        [ValidateSet('none','viewe-md80et-v10')][string]$BatteryBoard = 'none',
        [ValidateSet('vendor','pclk12')][string]$DisplayClock = 'vendor',
        [ValidateSet('none','counters')][string]$DisplayDiagnostic = 'none'
    )
    if (($DisplayClock -ne 'vendor' -or $DisplayDiagnostic -ne 'none') -and
        ($Mode -ne 'production' -or $BatteryBoard -ne 'none')) {
        throw 'Explicit display profiles require normal Wi-Fi firmware with battery telemetry disabled'
    }
    $extraFlags = @()
    if ($DisplayClock -eq 'pclk12') { $extraFlags += '-DGRIDOPOLY_DISPLAY_PCLK_HZ=12000000' }
    if ($DisplayDiagnostic -eq 'counters') { $extraFlags += '-DGRIDOPOLY_DISPLAY_DIAGNOSTIC=1' }
    [pscustomobject]@{
        displayClock = $DisplayClock
        requestedPclkHz = $(if ($DisplayClock -eq 'pclk12') { 12000000 } else { 16000000 })
        displayDiagnostic = $DisplayDiagnostic
        diagnosticOnly = ($DisplayDiagnostic -ne 'none')
        extraFlags = ($extraFlags -join ' ')
    }
}
