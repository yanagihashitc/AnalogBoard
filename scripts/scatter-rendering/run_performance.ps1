[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Official', 'DryRun')]
    [string]$Mode,

    [string]$OutputRoot,

    [string]$ReferenceProfile,

    [ValidateRange(1, 10000)]
    [int]$DryRunWarmupMilliseconds = 50,

    [ValidateRange(1, 10000)]
    [int]$DryRunMeasurementMilliseconds = 500,

    [ValidateRange(1, 10000)]
    [int]$DryRunSoakMilliseconds = 1000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $ScriptDir '..\..') -ErrorAction Stop).Path
$PrototypeRoot = (Resolve-Path -LiteralPath (Join-Path $RepositoryRoot 'prototypes\scatter-rendering') -ErrorAction Stop).Path
$ModulePath = Join-Path $ScriptDir 'scatter_verification_core.psm1'
$TestDllPath = Join-Path $PrototypeRoot 'tests\AnalogBoard.ScatterRendering.Tests\bin\x64\Release\net10.0-windows\AnalogBoard.ScatterRendering.Tests.dll'
$ExpectedSourceRevisionPattern = '^[0-9a-f]{40}$'
$script:PerformanceExitCode = 2
$sessionDirectory = $null
$GitExecutablePath = $null

function Write-AtomicJson {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Value
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    if (Test-Path -LiteralPath $fullPath) {
        throw [IO.IOException]::new("Atomic JSON target already exists: '$fullPath'.")
    }
    $parent = Split-Path -Parent $fullPath
    $null = New-Item -ItemType Directory -Path $parent -Force
    $partialPath = "$fullPath.$([guid]::NewGuid().ToString('N')).partial"
    try {
        $json = $Value | ConvertTo-Json -Depth 12
        $encoding = [Text.UTF8Encoding]::new($false)
        $stream = [IO.FileStream]::new(
            $partialPath,
            [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write,
            [IO.FileShare]::None,
            65536,
            [IO.FileOptions]::WriteThrough
        )
        try {
            $bytes = $encoding.GetBytes($json + [Environment]::NewLine)
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush($true)
        }
        finally {
            $stream.Dispose()
        }
        [IO.File]::Move($partialPath, $fullPath)
    }
    catch {
        Remove-Item -LiteralPath $partialPath -Force -ErrorAction SilentlyContinue
        throw
    }
}

function Get-SingleCimInstance {
    param(
        [Parameter(Mandatory = $true)][string]$ClassName,
        [string]$Filter
    )

    if ([string]::IsNullOrWhiteSpace($Filter)) {
        $values = @(Get-CimInstance -ClassName $ClassName -ErrorAction Stop)
    }
    else {
        $values = @(Get-CimInstance -ClassName $ClassName -Filter $Filter -ErrorAction Stop)
    }
    if ($values.Count -ne 1) {
        throw [InvalidOperationException]::new(
            "Performance identity requires exactly one $ClassName instance; actual: $($values.Count)."
        )
    }
    return $values[0]
}

function Get-NonEmptyIdentityString {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $text = [string]$Value
    if ([string]::IsNullOrWhiteSpace($text)) {
        throw [InvalidOperationException]::new(
            "Performance identity field is empty: $Name."
        )
    }
    return $text.Trim()
}

function Get-ActivePowerSchemeGuid {
    $output = @(& powercfg.exe /GETACTIVESCHEME 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) {
        throw [InvalidOperationException]::new(
            "powercfg /GETACTIVESCHEME failed: $($output -join ' ')"
        )
    }
    $match = [regex]::Match(($output -join ' '), '(?i)[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}')
    if (-not $match.Success) {
        throw [InvalidOperationException]::new(
            'Active power scheme output contains no GUID.'
        )
    }
    return $match.Value.ToLowerInvariant()
}

function Get-OutputStorageIdentity {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $driveRoot = [IO.Path]::GetPathRoot($fullPath)
    if ([string]::IsNullOrWhiteSpace($driveRoot)) {
        throw [InvalidOperationException]::new(
            "Performance output path has no drive root: '$fullPath'."
        )
    }
    $deviceId = $driveRoot.TrimEnd('\')
    $logicalDisk = Get-SingleCimInstance -ClassName 'Win32_LogicalDisk' -Filter "DeviceID='$deviceId'"
    $partitions = @(Get-CimAssociatedInstance -InputObject $logicalDisk -Association 'Win32_LogicalDiskToPartition' -ErrorAction Stop)
    $drives = @(
        $partitions |
            ForEach-Object {
                Get-CimAssociatedInstance -InputObject $_ -Association 'Win32_DiskDriveToDiskPartition' -ErrorAction Stop
            } |
            Sort-Object -Property DeviceID -Unique
    )
    if ($drives.Count -ne 1) {
        throw [InvalidOperationException]::new(
            "Performance output must resolve to exactly one physical disk; actual: $($drives.Count)."
        )
    }
    $drive = $drives[0]
    return [pscustomobject]@{
        Model = Get-NonEmptyIdentityString -Value $drive.Model -Name 'storage_model'
        Serial = Get-NonEmptyIdentityString -Value $drive.SerialNumber -Name 'storage_serial'
        BusType = Get-NonEmptyIdentityString -Value $drive.InterfaceType -Name 'storage_bus_type'
    }
}

function Get-LivePerformanceProfile {
    param(
        [Parameter(Mandatory = $true)][string]$OutputPath,
        [Parameter(Mandatory = $true)][string]$SdkVersion,
        [Parameter(Mandatory = $true)][string]$DesktopRuntimeVersion
    )

    $computer = Get-SingleCimInstance -ClassName 'Win32_ComputerSystem'
    $operatingSystem = Get-SingleCimInstance -ClassName 'Win32_OperatingSystem'
    $processor = Get-SingleCimInstance -ClassName 'Win32_Processor'
    $videoControllers = @(
        Get-CimInstance -ClassName 'Win32_VideoController' -ErrorAction Stop |
            Sort-Object -Property Name, DriverVersion
    )
    if ($videoControllers.Count -lt 1) {
        throw [InvalidOperationException]::new('Performance identity requires at least one GPU.')
    }
    $gpuNames = @($videoControllers | ForEach-Object {
        Get-NonEmptyIdentityString -Value $_.Name -Name 'gpu_name'
    })
    $gpuDrivers = @($videoControllers | ForEach-Object {
        Get-NonEmptyIdentityString -Value $_.DriverVersion -Name 'gpu_driver_version'
    })
    $activeVideo = @($videoControllers | Where-Object {
        $null -ne $_.CurrentHorizontalResolution -and
        $null -ne $_.CurrentVerticalResolution -and
        $null -ne $_.CurrentRefreshRate -and
        [int64]$_.CurrentHorizontalResolution -gt 0 -and
        [int64]$_.CurrentVerticalResolution -gt 0 -and
        [int64]$_.CurrentRefreshRate -gt 0
    })
    if ($activeVideo.Count -ne 1) {
        throw [InvalidOperationException]::new(
            "Performance identity requires exactly one active display mode; actual: $($activeVideo.Count)."
        )
    }

    if (-not ('P0R1NativeDisplay' -as [type])) {
        Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class P0R1NativeDisplay
{
    [DllImport("user32.dll")]
    public static extern uint GetDpiForSystem();
}
'@
    }
    $dpi = [int64][P0R1NativeDisplay]::GetDpiForSystem()
    if ($dpi -le 0) {
        throw [InvalidOperationException]::new('Performance identity returned an invalid system DPI.')
    }
    Add-Type -AssemblyName System.Windows.Forms
    $remoteSession = [Windows.Forms.SystemInformation]::TerminalServerSession -or
        ([string]$env:SESSIONNAME).StartsWith('RDP-', [StringComparison]::OrdinalIgnoreCase)
    $storage = Get-OutputStorageIdentity -Path $OutputPath

    return [ordered]@{
        schema_id = 'analogboard.scatter-rendering.reference-profile.v1'
        profile_id = 'AB-PERF-REF-v1'
        profile_status = 'live_observation'
        owner_approval_id = 'live-observation-not-owner-approval'
        manufacturer = Get-NonEmptyIdentityString -Value $computer.Manufacturer -Name 'manufacturer'
        model = Get-NonEmptyIdentityString -Value $computer.Model -Name 'model'
        machine_name = Get-NonEmptyIdentityString -Value $env:COMPUTERNAME -Name 'machine_name'
        os_product = Get-NonEmptyIdentityString -Value $operatingSystem.Caption -Name 'os_product'
        os_version = Get-NonEmptyIdentityString -Value $operatingSystem.Version -Name 'os_version'
        os_build = Get-NonEmptyIdentityString -Value $operatingSystem.BuildNumber -Name 'os_build'
        cpu = Get-NonEmptyIdentityString -Value $processor.Name -Name 'cpu'
        ram_bytes = [int64]$computer.TotalPhysicalMemory
        gpu_name = $gpuNames -join ' | '
        gpu_driver_version = $gpuDrivers -join ' | '
        display_width = [int64]$activeVideo[0].CurrentHorizontalResolution
        display_height = [int64]$activeVideo[0].CurrentVerticalResolution
        display_refresh_hz = [int64]$activeVideo[0].CurrentRefreshRate
        display_dpi_x = $dpi
        display_dpi_y = $dpi
        power_scheme_guid = Get-ActivePowerSchemeGuid
        storage_model = $storage.Model
        storage_serial = $storage.Serial
        storage_bus_type = $storage.BusType
        monotonic_clock = 'System.Diagnostics.Stopwatch'
        stopwatch_frequency = [Diagnostics.Stopwatch]::Frequency
        sdk_version = $SdkVersion
        desktop_runtime_version = $DesktopRuntimeVersion
        target_framework = 'net10.0-windows'
        configuration = 'Release'
        architecture = 'x64'
        remote_session = [bool]$remoteSession
    }
}

function Invoke-PerformanceChild {
    param(
        [Parameter(Mandatory = $true)][string]$Scenario,
        [Parameter(Mandatory = $true)][int]$RunIndex,
        [Parameter(Mandatory = $true)][string]$OutputPath,
        [Parameter(Mandatory = $true)][string]$ObservedProfilePath,
        [Parameter(Mandatory = $true)][string]$ProvenancePath,
        [string]$ReferenceProfilePath
    )

    $modeArgument = if ($Mode -ceq 'Official') { 'official' } else { 'dry-run' }
    $arguments = @(
        'exec',
        $TestDllPath,
        'perf',
        $modeArgument,
        '--scenario', $Scenario,
        '--run-index', $RunIndex.ToString([Globalization.CultureInfo]::InvariantCulture),
        '--repository-root', $RepositoryRoot,
        '--output-root', $OutputRoot,
        '--output', $OutputPath,
        '--observed-profile', $ObservedProfilePath,
        '--provenance', $ProvenancePath
    )
    if ($Mode -ceq 'Official') {
        $arguments += @('--reference-profile', $ReferenceProfilePath)
    }
    else {
        $arguments += @(
            '--warmup-ms', $DryRunWarmupMilliseconds.ToString([Globalization.CultureInfo]::InvariantCulture),
            '--measurement-ms', $DryRunMeasurementMilliseconds.ToString([Globalization.CultureInfo]::InvariantCulture),
            '--soak-ms', $DryRunSoakMilliseconds.ToString([Globalization.CultureInfo]::InvariantCulture)
        )
    }
    $result = Invoke-P0R1SanitizedDotNet `
        -Arguments $arguments `
        -WorkingDirectory $PrototypeRoot `
        -GitExecutablePath $GitExecutablePath `
        -OfficialPreflight:($Mode -ceq 'Official')
    foreach ($line in $result.Output) {
        Write-Host $line
    }
    if ($result.ExitCode -ne 0) {
        $script:PerformanceExitCode = $result.ExitCode
        Write-PerformanceFailureEvidence `
            -SessionDirectory (Split-Path -Parent (Split-Path -Parent $OutputPath)) `
            -Stage 'child' `
            -Scenario $Scenario `
            -RunIndex $RunIndex `
            -ExitCode $result.ExitCode `
            -Output $result.Output
        throw [InvalidOperationException]::new(
            "Performance child failed for $Scenario run $RunIndex with exit $($result.ExitCode)."
        )
    }

    $artifact = Get-Content -LiteralPath $OutputPath -Raw -Encoding UTF8 | ConvertFrom-Json
    return [pscustomobject][ordered]@{
        scenario = $Scenario
        run_index = $RunIndex
        process_id = [int]$artifact.process_id
        exit_code = [int]$result.ExitCode
    }
}

function Write-PerformanceFailureEvidence {
    param(
        [Parameter(Mandatory = $true)][string]$SessionDirectory,
        [Parameter(Mandatory = $true)][string]$Stage,
        [string]$Scenario,
        [int]$RunIndex,
        [Parameter(Mandatory = $true)][int]$ExitCode,
        [string[]]$Output = @()
    )

    try {
        if (-not (Test-Path -LiteralPath $SessionDirectory -PathType Container)) {
            return
        }
        $sealed = Test-Path `
            -LiteralPath (Join-Path $SessionDirectory 'suite.manifest.json') `
            -PathType Leaf
        $path = if ($sealed) {
            "$SessionDirectory.failure.json"
        }
        else {
            Join-Path $SessionDirectory 'failure.json'
        }
        if (Test-Path -LiteralPath $path) {
            return
        }
        Write-AtomicJson -Path $path -Value ([ordered]@{
            schema_id = 'analogboard.scatter-rendering.failure.v1'
            runner_contract_id = 'AB-PERF-RUNNER-v1'
            recorded_at_utc = [DateTime]::UtcNow.ToString('O', [Globalization.CultureInfo]::InvariantCulture)
            stage = $Stage
            scenario = $Scenario
            run_index = $RunIndex
            exit_code = $ExitCode
            output = @($Output)
        })
    }
    catch {
        [Console]::Error.WriteLine("Unable to seal performance failure evidence: $($_.Exception.Message)")
    }
}

try {
    Import-Module $ModulePath -Force -ErrorAction Stop
    if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
        $OutputRoot = Join-Path $RepositoryRoot 'artifacts\phase0-scatter-rendering'
    }
    $null = New-Item -ItemType Directory -Path $OutputRoot -Force
    $OutputRoot = (Resolve-Path -LiteralPath $OutputRoot -ErrorAction Stop).Path

    if ($Mode -ceq 'Official') {
        $canonicalOutputRoot = Join-Path $RepositoryRoot 'artifacts\phase0-scatter-rendering'
        if (-not [string]::Equals(
                $OutputRoot,
                $canonicalOutputRoot,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw [InvalidOperationException]::new(
                "Official mode requires the canonical ignored output root: '$canonicalOutputRoot'."
            )
        }
        if ([string]::IsNullOrWhiteSpace($ReferenceProfile)) {
            throw [ArgumentException]::new(
                'Official mode requires -ReferenceProfile.',
                'ReferenceProfile'
            )
        }
        $ReferenceProfile = (Resolve-Path -LiteralPath $ReferenceProfile -ErrorAction Stop).Path
        $canonicalRelativeProfile =
            'docs/reference/scatter-rendering/phase0/performance-reference-profile-v1.json'
        $canonicalReferenceProfile = Join-Path $RepositoryRoot $canonicalRelativeProfile
        if (-not [string]::Equals(
                $ReferenceProfile,
                $canonicalReferenceProfile,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw [InvalidOperationException]::new(
                "Official mode requires the canonical tracked AB-PERF-REF-v1 path: '$canonicalReferenceProfile'."
            )
        }
        $trackedReference = @(& git -C $RepositoryRoot ls-files --error-unmatch -- $canonicalRelativeProfile 2>$null)
        if ($LASTEXITCODE -ne 0 -or $trackedReference.Count -ne 1) {
            throw [InvalidOperationException]::new(
                'Official mode requires a Git-tracked AB-PERF-REF-v1 profile.'
            )
        }
    }

    $null = Assert-P0R1RepositoryDependencyContract -RepositoryRoot $RepositoryRoot
    $null = Assert-P0R1RendererDecisionContract -RepositoryRoot $RepositoryRoot
    $verification = Invoke-P0R1FocusedVerification `
        -RepositoryRoot $RepositoryRoot `
        -PrototypeRoot $PrototypeRoot `
        -Configuration 'Release' `
        -Architecture 'x64'
    if ($verification.Status -cne 'Pass' -or -not $verification.ProductTestsExecuted) {
        throw [InvalidOperationException]::new('Focused verification did not pass before performance execution.')
    }
    if (-not (Test-Path -LiteralPath $TestDllPath -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new('Performance test executable is absent after build.', $TestDllPath)
    }
    if ($Mode -ceq 'Official') {
        $GitExecutablePath = (Get-Command git.exe -CommandType Application -ErrorAction Stop).Source
    }

    $sdkResult = Invoke-P0R1SanitizedDotNet -Arguments @('--version') -WorkingDirectory $PrototypeRoot
    $runtimeResult = Invoke-P0R1SanitizedDotNet -Arguments @('--list-runtimes') -WorkingDirectory $PrototypeRoot
    if ($sdkResult.ExitCode -ne 0 -or $runtimeResult.ExitCode -ne 0) {
        throw [InvalidOperationException]::new('Pinned dotnet toolchain inventory failed.')
    }
    $toolchain = Assert-P0R1ToolchainOutput `
        -SdkVersionLines $sdkResult.Output `
        -RuntimeLines $runtimeResult.Output

    $sourceRevision = (& git -C $RepositoryRoot rev-parse HEAD).Trim().ToLowerInvariant()
    if ($LASTEXITCODE -ne 0 -or $sourceRevision -notmatch $ExpectedSourceRevisionPattern) {
        throw [InvalidOperationException]::new('Unable to resolve an exact Git source revision.')
    }
    $sourceDirty = @(& git -C $RepositoryRoot status --porcelain --untracked-files=normal).Count -ne 0
    if ($Mode -ceq 'Official' -and $sourceDirty) {
        throw [InvalidOperationException]::new('Official performance execution requires a clean tracked worktree.')
    }

    $sessionId = '{0}-{1}-{2}' -f `
        [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ', [Globalization.CultureInfo]::InvariantCulture), `
        $PID, `
        [guid]::NewGuid().ToString('N').Substring(0, 8)
    $sessionDirectory = Join-Path $OutputRoot "$sessionId.inprogress"
    $runsDirectory = Join-Path $sessionDirectory 'runs'
    $null = New-Item -ItemType Directory -Path $runsDirectory

    $observedProfilePath = Join-Path $sessionDirectory 'profile.actual.json'
    $observedProfile = Get-LivePerformanceProfile `
        -OutputPath $sessionDirectory `
        -SdkVersion $toolchain.SdkVersion `
        -DesktopRuntimeVersion $toolchain.DesktopRuntimeVersion
    Write-AtomicJson -Path $observedProfilePath -Value $observedProfile

    $provenancePath = Join-Path $sessionDirectory 'provenance.json'
    $provenance = [ordered]@{
        schema_id = 'analogboard.scatter-rendering.provenance.v1'
        source_revision = $sourceRevision
        source_dirty = [bool]$sourceDirty
        sdk_version = $toolchain.SdkVersion
        desktop_runtime_version = $toolchain.DesktopRuntimeVersion
        target_framework = 'net10.0-windows'
        configuration = 'Release'
        architecture = 'x64'
    }
    Write-AtomicJson -Path $provenancePath -Value $provenance

    $processExitRecords = [Collections.Generic.List[object]]::new()

    foreach ($scenario in @('hard-scatter', 'hard-combined')) {
        foreach ($runIndex in 1..3) {
            $filePrefix = if ($scenario -ceq 'hard-scatter') { 'hard-scatter' } else { 'hard-combined' }
            $outputPath = Join-Path $runsDirectory ('{0}-{1:D2}.raw.json' -f $filePrefix, $runIndex)
            $processExitRecords.Add((Invoke-PerformanceChild `
                -Scenario $scenario `
                -RunIndex $runIndex `
                -OutputPath $outputPath `
                -ObservedProfilePath $observedProfilePath `
                -ProvenancePath $provenancePath `
                -ReferenceProfilePath $ReferenceProfile))
        }
    }
    $processExitRecords.Add((Invoke-PerformanceChild `
        -Scenario 'soak' `
        -RunIndex 1 `
        -OutputPath (Join-Path $runsDirectory 'soak-01.raw.json') `
        -ObservedProfilePath $observedProfilePath `
        -ProvenancePath $provenancePath `
        -ReferenceProfilePath $ReferenceProfile))
    $processExitRecords.Add((Invoke-PerformanceChild `
        -Scenario 'headroom' `
        -RunIndex 1 `
        -OutputPath (Join-Path $runsDirectory 'headroom-01.raw.json') `
        -ObservedProfilePath $observedProfilePath `
        -ProvenancePath $provenancePath `
        -ReferenceProfilePath $ReferenceProfile))

    $processExitsPath = Join-Path $sessionDirectory 'process-exits.json'
    Write-AtomicJson -Path $processExitsPath -Value ([ordered]@{
        schema_id = 'analogboard.scatter-rendering.process-exits.v1'
        runner_contract_id = 'AB-PERF-RUNNER-v1'
        records = @($processExitRecords)
    })

    $finalObservedProfilePath = Join-Path $sessionDirectory 'profile.final.json'
    $finalObservedProfile = Get-LivePerformanceProfile `
        -OutputPath $sessionDirectory `
        -SdkVersion $toolchain.SdkVersion `
        -DesktopRuntimeVersion $toolchain.DesktopRuntimeVersion
    Write-AtomicJson -Path $finalObservedProfilePath -Value $finalObservedProfile

    $finalizeArguments = @(
        'exec',
        $TestDllPath,
        'perf',
        'finalize',
        $(if ($Mode -ceq 'Official') { 'official' } else { 'dry-run' }),
        '--repository-root', $RepositoryRoot,
        '--output-root', $OutputRoot,
        '--session-dir', $sessionDirectory,
        '--observed-profile', $observedProfilePath,
        '--final-observed-profile', $finalObservedProfilePath,
        '--provenance', $provenancePath,
        '--process-exits', $processExitsPath
    )
    if ($Mode -ceq 'Official') {
        $finalizeArguments += @('--reference-profile', $ReferenceProfile)
    }
    $finalizeResult = Invoke-P0R1SanitizedDotNet `
        -Arguments $finalizeArguments `
        -WorkingDirectory $PrototypeRoot `
        -GitExecutablePath $GitExecutablePath `
        -OfficialPreflight:($Mode -ceq 'Official')
    foreach ($line in $finalizeResult.Output) {
        Write-Host $line
    }
    if ($finalizeResult.ExitCode -ne 0) {
        $script:PerformanceExitCode = $finalizeResult.ExitCode
        Write-PerformanceFailureEvidence `
            -SessionDirectory $sessionDirectory `
            -Stage 'finalizer' `
            -RunIndex 0 `
            -ExitCode $finalizeResult.ExitCode `
            -Output $finalizeResult.Output
        throw [InvalidOperationException]::new(
            "Performance suite finalizer failed with exit $($finalizeResult.ExitCode)."
        )
    }
    exit 0
}
catch {
    [Console]::Error.WriteLine("P0-R1 performance execution failed: $($_.Exception.Message)")
    [Console]::Error.WriteLine("P0-R1 performance failure location: $($_.ScriptStackTrace)")
    exit $script:PerformanceExitCode
}
