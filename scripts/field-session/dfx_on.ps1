# Revert the DFX setting to the driver default (remove the override) and
# restart the device. Run this after experiment 2 to restore the machine.
# MUST be run from an *administrator* PowerShell:
#   Right-click Start button -> "Terminal (Admin)" -> Yes
#   powershell -ExecutionPolicy Bypass -File .\dfx_on.ps1

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host 'NG: administrator rights required. Right-click Start -> "Terminal (Admin)" and retry.' -ForegroundColor Red
    exit 1
}
$devices = Get-PnpDevice | Where-Object { $_.InstanceId -like 'USB\VID_04B4&PID_FFF*' }
if (-not $devices) {
    Write-Host 'NG: FX3 board (VID_04B4) not found. Check the USB connection.' -ForegroundColor Red
    exit 1
}
foreach ($d in $devices) {
    $key = "HKLM:\SYSTEM\CurrentControlSet\Enum\$($d.InstanceId)\Device Parameters\WDF"
    Remove-ItemProperty -Path $key -Name WdfDirectedPowerTransitionEnable -ErrorAction SilentlyContinue
    Write-Host "Removed NoDfx override on $($d.InstanceId) (back to driver default)"
    pnputil /restart-device "$($d.InstanceId)"
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'WARN: device restart failed. Unplug and replug the USB connector instead.' -ForegroundColor Yellow
    }
}
Write-Host ''
Write-Host 'Done. Now run dfx_status.ps1 and confirm it shows "not set (driver default)".' -ForegroundColor Green
