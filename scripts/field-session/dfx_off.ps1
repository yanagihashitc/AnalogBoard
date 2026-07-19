# Disable DFX (set WdfDirectedPowerTransitionEnable=0) for the FX3 board and
# restart the device so the change takes effect.
# MUST be run from an *administrator* PowerShell:
#   Right-click Start button -> "Terminal (Admin)" -> Yes
#   powershell -ExecutionPolicy Bypass -File .\dfx_off.ps1
# Do NOT edit cyusb3.inf and do NOT use the CyUsb3NoDfx INF section
# (breaks the signed .cat / changes two variables at once).

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
    New-Item -Path $key -Force | Out-Null
    New-ItemProperty -Path $key -Name WdfDirectedPowerTransitionEnable -PropertyType DWord -Value 0 -Force | Out-Null
    Write-Host "Set NoDfx (=0) on $($d.InstanceId)"
    pnputil /restart-device "$($d.InstanceId)"
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'NG: device restart failed. Do not unplug USB. Preserve this output and stop for owner review.' -ForegroundColor Red
        exit 2
    }
}
Write-Host ''
Write-Host 'Done. Now run dfx_status.ps1 and confirm it shows "0 = DFX DISABLED".' -ForegroundColor Green
