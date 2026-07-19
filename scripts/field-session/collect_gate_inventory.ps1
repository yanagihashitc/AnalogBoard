# Collect read-only system provenance before AnalogBoard Gate B/C measurements.
# This script writes evidence files only. It does not change drivers, devices,
# power settings, application files, or measurement data.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('GateB', 'GateC', 'GateCRollback')]
    [string]$Stage,

    [Parameter(Mandatory = $true)]
    [string]$PackageRoot,

    [Parameter(Mandatory = $true)]
    [AllowEmptyString()]
    [string]$SampleId,

    [Parameter(Mandatory = $true)]
    [AllowEmptyString()]
    [string]$UsbPortNote,

    [Parameter(Mandatory = $true)]
    [AllowEmptyString()]
    [string]$CableConfiguration,

    [string]$OutputRoot,

    [ValidatePattern('^[A-Za-z]$')]
    [string]$DataDriveLetter = 'D',

    [string]$DeviceInstanceId,

    [string]$DeviceInstanceIdPattern = 'USB\VID_04B4&PID_FFF*',

    [string]$Note = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$CollectorVersion = 'GateInventory-v1.2'
$ExpectedBuildId = 'r7-driver-telemetry-graceful-stop-20260716T1314JST'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $ScriptDir 'gate_inventory_core.psm1') -Force

function Get-DevicePropertyData {
    param(
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [Parameter(Mandatory = $true)][string]$KeyName
    )

    try {
        return (Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $KeyName -ErrorAction Stop).Data
    }
    catch {
        return $null
    }
}

function Get-UsbDeviceRecord {
    param(
        [Parameter(Mandatory = $true)]$Device,
        [Parameter(Mandatory = $true)][int]$Depth
    )

    $instanceId = [string]$Device.InstanceId
    $locationPaths = @(Get-DevicePropertyData -InstanceId $instanceId -KeyName 'DEVPKEY_Device_LocationPaths')
    [pscustomobject]@{
        Depth = $Depth
        FriendlyName = [string]$Device.FriendlyName
        Class = [string]$Device.Class
        Status = [string]$Device.Status
        InstanceId = $instanceId
        ParentInstanceId = [string](Get-DevicePropertyData -InstanceId $instanceId -KeyName 'DEVPKEY_Device_Parent')
        LocationInfo = [string](Get-DevicePropertyData -InstanceId $instanceId -KeyName 'DEVPKEY_Device_LocationInfo')
        LocationPaths = ($locationPaths -join '; ')
        BusReportedDescription = [string](Get-DevicePropertyData -InstanceId $instanceId -KeyName 'DEVPKEY_Device_BusReportedDeviceDesc')
        Service = [string](Get-DevicePropertyData -InstanceId $instanceId -KeyName 'DEVPKEY_Device_Service')
        DriverVersion = [string](Get-DevicePropertyData -InstanceId $instanceId -KeyName 'DEVPKEY_Device_DriverVersion')
        DriverProvider = [string](Get-DevicePropertyData -InstanceId $instanceId -KeyName 'DEVPKEY_Device_DriverProvider')
        DriverInfPath = [string](Get-DevicePropertyData -InstanceId $instanceId -KeyName 'DEVPKEY_Device_DriverInfPath')
        DriverDate = [string](Get-DevicePropertyData -InstanceId $instanceId -KeyName 'DEVPKEY_Device_DriverDate')
    }
}

function Get-UsbParentChain {
    param([Parameter(Mandatory = $true)]$Device)

    $records = @()
    $seen = @{}
    $current = $Device
    for ($depth = 0; $depth -lt 16 -and $null -ne $current; $depth++) {
        $record = Get-UsbDeviceRecord -Device $current -Depth $depth
        if ($seen.ContainsKey($record.InstanceId)) {
            break
        }
        $seen[$record.InstanceId] = $true
        $records += $record
        if ([string]::IsNullOrWhiteSpace($record.ParentInstanceId)) {
            break
        }
        try {
            $current = Get-PnpDevice -InstanceId $record.ParentInstanceId -ErrorAction Stop
        }
        catch {
            $records += [pscustomobject]@{
                Depth = $depth + 1
                FriendlyName = '<parent lookup failed>'
                Class = ''
                Status = ''
                InstanceId = $record.ParentInstanceId
                ParentInstanceId = ''
                LocationInfo = ''
                LocationPaths = ''
                BusReportedDescription = ''
                Service = ''
                DriverVersion = ''
                DriverProvider = ''
                DriverInfPath = ''
                DriverDate = ''
            }
            break
        }
    }
    return $records
}

$resolvedPackageRoot = (Resolve-Path -LiteralPath $PackageRoot -ErrorAction Stop).Path
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $resolvedPackageRoot 'evidence\inventory'
}
$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$outputDir = Join-Path $OutputRoot ($timestamp + '-' + $Stage)
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

try {
    $manifestPath = Join-Path $resolvedPackageRoot 'manifest\build_manifest.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $buildId = Assert-GateBuildId -CurrentBuildId ([string]$manifest.build_id) -ExpectedBuildId $ExpectedBuildId
    $artifacts = @(Test-GatePackageArtifacts -PackageRoot $resolvedPackageRoot -BuildManifestPath $manifestPath)
    $artifacts | Export-Csv (Join-Path $outputDir 'artifact_hashes.csv') -NoTypeInformation -Encoding UTF8

    $matchingDevices = ConvertTo-GateObjectArray -InputObject @(
        if ([string]::IsNullOrWhiteSpace($DeviceInstanceId)) {
            Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like $DeviceInstanceIdPattern }
        }
        else {
            Get-PnpDevice -PresentOnly -InstanceId $DeviceInstanceId -ErrorAction Stop
        }
    )
    if ($matchingDevices.Count -eq 0) {
        throw "FX3 device was not found. Expected InstanceId pattern '$DeviceInstanceIdPattern'."
    }
    if ($matchingDevices.Count -gt 1) {
        $ids = @($matchingDevices | ForEach-Object { $_.InstanceId }) -join ', '
        throw "Multiple FX3 devices were found. Re-run with -DeviceInstanceId. Devices: $ids"
    }

    $fx3 = $matchingDevices[0]
    $usbChain = @(Get-UsbParentChain -Device $fx3)
    $usbChain | Export-Csv (Join-Path $outputDir 'usb_parent_chain.csv') -NoTypeInformation -Encoding UTF8
    $driverVersion = [string]$usbChain[0].DriverVersion
    $context = Assert-GateInventoryContext `
        -Stage $Stage `
        -CurrentDriverVersion $driverVersion `
        -SampleId $SampleId `
        -UsbPortNote $UsbPortNote `
        -CableConfiguration $CableConfiguration

    $computerSystem = Get-CimInstance -ClassName Win32_ComputerSystem
    $operatingSystem = Get-CimInstance -ClassName Win32_OperatingSystem
    $processors = @(Get-CimInstance -ClassName Win32_Processor)
    $computer = [pscustomobject]@{
        Manufacturer = [string]$computerSystem.Manufacturer
        Model = [string]$computerSystem.Model
        Name = [string]$computerSystem.Name
        TotalPhysicalMemoryBytes = [long]$computerSystem.TotalPhysicalMemory
        OsCaption = [string]$operatingSystem.Caption
        OsVersion = [string]$operatingSystem.Version
        OsBuildNumber = [string]$operatingSystem.BuildNumber
        OsArchitecture = [string]$operatingSystem.OSArchitecture
        Cpu = (@($processors | ForEach-Object { $_.Name }) -join '; ')
    }
    $computer | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $outputDir 'computer.json') -Encoding UTF8

    $driveLetter = $DataDriveLetter.ToUpperInvariant()
    $partition = Get-Partition -DriveLetter $driveLetter -ErrorAction Stop
    $disk = Get-Disk -Number $partition.DiskNumber -ErrorAction Stop
    $volume = Get-Volume -DriveLetter $driveLetter -ErrorAction Stop
    $dataDisk = [pscustomobject]@{
        DriveLetter = $driveLetter
        VolumeFileSystemLabel = [string]$volume.FileSystemLabel
        VolumeFileSystem = [string]$volume.FileSystem
        VolumeSizeBytes = [long]$volume.Size
        VolumeSizeRemainingBytes = [long]$volume.SizeRemaining
        DiskNumber = [int]$disk.Number
        FriendlyName = [string]$disk.FriendlyName
        SerialNumber = [string]$disk.SerialNumber
        BusType = [string]$disk.BusType
        SizeBytes = [long]$disk.Size
        PartitionStyle = [string]$disk.PartitionStyle
        OperationalStatus = (@($disk.OperationalStatus) -join '; ')
        HealthStatus = [string]$disk.HealthStatus
        UniqueId = [string]$disk.UniqueId
    }
    $dataDisk | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $outputDir 'data_disk.json') -Encoding UTF8

    $powerPlanOutput = @(& powercfg.exe /getactivescheme 2>&1)
    $powerPlanExitCode = $LASTEXITCODE
    $powerPlanOutput | Set-Content (Join-Path $outputDir 'active_power_plan.txt') -Encoding UTF8
    if ($powerPlanExitCode -ne 0) {
        throw "powercfg /getactivescheme failed with exit code $powerPlanExitCode."
    }

    $buildManifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksumManifestPath = Join-Path $resolvedPackageRoot 'manifest\checksums.sha256'
    $checksumManifestHash = if (Test-Path -LiteralPath $checksumManifestPath -PathType Leaf) {
        (Get-FileHash -LiteralPath $checksumManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    else {
        $null
    }
    $timeZone = Get-TimeZone
    $inventory = [pscustomobject]@{
        CollectorVersion = $CollectorVersion
        CapturedAtLocal = (Get-Date).ToString('o')
        CapturedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        TimeZoneId = [string]$timeZone.Id
        Stage = $context.Stage
        ExpectedDriverVersion = $context.ExpectedDriverVersion
        CurrentDriverVersion = $context.CurrentDriverVersion
        SampleId = $context.SampleId
        UsbPortNote = $context.UsbPortNote
        CableConfiguration = $context.CableConfiguration
        Note = $Note
        PackageRoot = $resolvedPackageRoot
        BuildId = $buildId
        BuildManifestSha256 = $buildManifestHash
        ChecksumManifestSha256 = $checksumManifestHash
        DataDriveLetter = $driveLetter
        Fx3InstanceId = [string]$fx3.InstanceId
        Fx3FriendlyName = [string]$fx3.FriendlyName
        Fx3LocationPaths = [string]$usbChain[0].LocationPaths
        Fx3DriverProvider = [string]$usbChain[0].DriverProvider
        Fx3DriverInfPath = [string]$usbChain[0].DriverInfPath
        ActivePowerPlan = ($powerPlanOutput -join [Environment]::NewLine)
        ArtifactHashes = $artifacts
        UsbParentChain = $usbChain
        Computer = $computer
        DataDisk = $dataDisk
        Result = 'PASS'
    }
    $inventory | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $outputDir 'inventory.json') -Encoding UTF8

    @(
        "collector=$CollectorVersion"
        "result=PASS"
        "captured_at_local=$($inventory.CapturedAtLocal)"
        "stage=$($context.Stage)"
        "build_id=$($inventory.BuildId)"
        "driver_version=$($context.CurrentDriverVersion)"
        "sample_id=$($context.SampleId)"
        "usb_port_note=$($context.UsbPortNote)"
        "cable_configuration=$($context.CableConfiguration)"
        "fx3_instance_id=$($inventory.Fx3InstanceId)"
        "fx3_location_paths=$($inventory.Fx3LocationPaths)"
        "data_drive=$driveLetter`:"
        "data_disk=$($dataDisk.FriendlyName)"
        "data_disk_bus_type=$($dataDisk.BusType)"
        "output_dir=$outputDir"
    ) | Set-Content (Join-Path $outputDir 'summary.txt') -Encoding UTF8

    Write-Host "OK: Gate inventory captured: $outputDir" -ForegroundColor Green
    exit 0
}
catch {
    $message = $_.Exception.Message
    @(
        "collector=$CollectorVersion"
        'result=FAIL'
        "captured_at_local=$((Get-Date).ToString('o'))"
        "stage=$Stage"
        "error=$message"
        "output_dir=$outputDir"
    ) | Set-Content (Join-Path $outputDir 'collector_error.txt') -Encoding UTF8
    [Console]::Error.WriteLine("Gate inventory failed: $message Evidence: $outputDir")
    exit 2
}
