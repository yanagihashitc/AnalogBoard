# Show the current DFX (Directed Power) setting for the FX3 board.
# No administrator rights required. Safe to run any time.
# Usage: powershell -ExecutionPolicy Bypass -File .\dfx_status.ps1

$devices = Get-PnpDevice | Where-Object { $_.InstanceId -like 'USB\VID_04B4&PID_FFF*' }
if (-not $devices) {
    Write-Host 'NG: FX3 board (VID_04B4) not found. Check the USB connection.' -ForegroundColor Red
    exit 1
}
foreach ($d in $devices) {
    $key = "HKLM:\SYSTEM\CurrentControlSet\Enum\$($d.InstanceId)\Device Parameters\WDF"
    $v = (Get-ItemProperty -Path $key -Name WdfDirectedPowerTransitionEnable -ErrorAction SilentlyContinue).WdfDirectedPowerTransitionEnable
    $state = if ($null -eq $v) { 'not set (driver default = DFX ENABLED)' }
             elseif ($v -eq 0)  { '0 = DFX DISABLED (NoDfx)' }
             else               { "$v = DFX ENABLED" }
    Write-Host ''
    Write-Host "Device : $($d.FriendlyName)"
    Write-Host "  Id     : $($d.InstanceId)"
    Write-Host "  Status : $($d.Status)"
    Write-Host "  WdfDirectedPowerTransitionEnable : $state"
}
Write-Host ''
Write-Host 'OK: status displayed. Record the value on the judgment sheet.' -ForegroundColor Green
