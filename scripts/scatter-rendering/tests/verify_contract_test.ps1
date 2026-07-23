$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$VerificationRoot = Split-Path -Parent $ScriptDir
$RepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $VerificationRoot '..\..')).Path
$ModulePath = Join-Path $VerificationRoot 'scatter_verification_core.psm1'
$WrapperPath = Join-Path $VerificationRoot 'verify.ps1'
$PerformanceWrapperPath = Join-Path $VerificationRoot 'run_performance.ps1'

if (-not (Test-Path -LiteralPath $ModulePath -PathType Leaf)) {
    throw "RED: scatter verification core is not implemented: $ModulePath"
}
if (-not (Test-Path -LiteralPath $WrapperPath -PathType Leaf)) {
    throw "RED: scatter verification wrapper is not implemented: $WrapperPath"
}
if (-not (Test-Path -LiteralPath $PerformanceWrapperPath -PathType Leaf)) {
    throw "RED: scatter performance wrapper is not implemented: $PerformanceWrapperPath"
}

Import-Module $ModulePath -Force

$performanceWrapper = Get-Content -LiteralPath $PerformanceWrapperPath -Raw -Encoding UTF8
$verificationWrapper = Get-Content -LiteralPath $WrapperPath -Raw -Encoding UTF8
$reviewBoundaryFailures = [System.Collections.Generic.List[string]]::new()
$modeNormalization =
    '$Mode = if ($Mode -ieq ''Official'') { ''Official'' } else { ''DryRun'' }'
$modeNormalizationIndex = $performanceWrapper.IndexOf(
    $modeNormalization,
    [StringComparison]::Ordinal
)
$firstCaseSensitiveModeCheckIndex = $performanceWrapper.IndexOf(
    '$Mode -ceq ''Official''',
    [StringComparison]::Ordinal
)
if ($modeNormalizationIndex -lt 0 -or
    $firstCaseSensitiveModeCheckIndex -lt 0 -or
    $modeNormalizationIndex -gt $firstCaseSensitiveModeCheckIndex) {
    $reviewBoundaryFailures.Add(
        'Performance wrapper must canonicalize ValidateSet mode casing before case-sensitive routing.'
    )
}

$gitStatusCapture = @'
$gitStatusOutput = @(
        & git -C $RepositoryRoot status --porcelain --untracked-files=normal 2>$null |
            ForEach-Object { $_.ToString() }
    )
'@
$gitStatusCaptureIndex = $performanceWrapper.IndexOf(
    $gitStatusCapture,
    [StringComparison]::Ordinal
)
$gitStatusExitGuardIndex = $performanceWrapper.IndexOf(
    '$gitStatusExitCode = $LASTEXITCODE',
    [StringComparison]::Ordinal
)
$gitStatusFailureIndex = $performanceWrapper.IndexOf(
    "throw [InvalidOperationException]::new('Unable to determine Git worktree status.')",
    [StringComparison]::Ordinal
)
$gitStatusDirtyIndex = $performanceWrapper.IndexOf(
    '$sourceDirty = $gitStatusOutput.Count -ne 0',
    [StringComparison]::Ordinal
)
# Given: Git status can fail or emit benign stderr before returning porcelain stdout.
# When: The performance wrapper derives its source-dirty provenance.
# Then: It isolates stderr, checks the native exit code, and only then derives dirty state.
if ($gitStatusCaptureIndex -lt 0 -or
    $gitStatusExitGuardIndex -lt $gitStatusCaptureIndex -or
    $gitStatusFailureIndex -lt $gitStatusExitGuardIndex -or
    $gitStatusDirtyIndex -lt $gitStatusFailureIndex) {
    $reviewBoundaryFailures.Add(
        'Performance wrapper must isolate Git stderr and fail closed when worktree status cannot be determined.'
    )
}

$focusedVerificationIndex = $verificationWrapper.IndexOf(
    'Invoke-P0R1FocusedVerification',
    [StringComparison]::Ordinal
)
$productTestsGuardIndex = $verificationWrapper.IndexOf(
    'if (-not $result.ProductTestsExecuted)',
    [StringComparison]::Ordinal
)
$productTestsFailureIndex = $verificationWrapper.IndexOf(
    "'Focused verification requires the complete P0-R1 prototype; product tests were not executed.'",
    [StringComparison]::Ordinal
)
$verificationSuccessIndex = $verificationWrapper.IndexOf(
    'Write-Host "P0-R1 verification status=',
    [StringComparison]::Ordinal
)
# Given: The prototype state helper can report the legacy zero-project ContractOnly state.
# When: The standalone mandatory verification wrapper receives that result.
# Then: It fails before publishing a successful verification status.
if ($focusedVerificationIndex -lt 0 -or
    $productTestsGuardIndex -lt $focusedVerificationIndex -or
    $productTestsFailureIndex -lt $productTestsGuardIndex -or
    $verificationSuccessIndex -lt $productTestsFailureIndex -or
    $verificationWrapper.Substring(
        $productTestsGuardIndex,
        $verificationSuccessIndex - $productTestsGuardIndex
    ) -notmatch 'throw\s+\[InvalidOperationException\]::new') {
    $reviewBoundaryFailures.Add(
        'Standalone focused verification must fail closed when product tests are not executed.'
    )
}

$prototypeStringsPath = Join-Path `
    $RepositoryRoot `
    'prototypes\scatter-rendering\src\AnalogBoard.ScatterRendering.Wpf\PrototypeStrings.cs'
$prototypeResourcesPath = Join-Path `
    $RepositoryRoot `
    'prototypes\scatter-rendering\src\AnalogBoard.ScatterRendering.Wpf\Properties\Resources.resx'
$prototypeStringsSource = Get-Content -LiteralPath $prototypeStringsPath -Raw -Encoding UTF8
[xml]$prototypeResources = Get-Content -LiteralPath $prototypeResourcesPath -Raw -Encoding UTF8
if ($null -eq $prototypeResources.SelectSingleNode(
        "/root/data[@name='RequiredPrototypeResourceAbsent']"
    ) -or
    $prototypeStringsSource.Contains('Required prototype resource is absent:')) {
    $reviewBoundaryFailures.Add(
        'Missing-resource exception text must be supplied only by Resources.resx.'
    )
}
if ($reviewBoundaryFailures.Count -ne 0) {
    throw "RED: $($reviewBoundaryFailures -join ' | ')"
}

foreach ($requiredPerformanceBoundary in @(
    'Assert-P0R1RepositoryDependencyContract',
    'Assert-P0R1RendererDecisionContract',
    'Invoke-P0R1FocusedVerification',
    'Invoke-P0R1SanitizedDotNet',
    'performance-reference-profile-v1.json',
    '.inprogress',
    '--warmup-ms',
    '--measurement-ms',
    '--soak-ms',
    '--reference-profile',
    '--repository-root',
    '--output-root',
    '--final-observed-profile',
    '--process-exits',
    'profile.final.json',
    'process-exits.json',
    'failure.json',
    '"$SessionDirectory.failure.json"',
    'suite.manifest.json',
    'Get-Command git.exe',
    '-OfficialPreflight:($Mode -ceq ''Official'')',
    'exit $script:PerformanceExitCode',
    "@('hard-scatter', 'hard-combined')",
    "-Scenario 'soak'",
    "-Scenario 'headroom'",
    "'finalize'"
)) {
    if (-not $performanceWrapper.Contains($requiredPerformanceBoundary)) {
        throw "Performance wrapper boundary is absent: $requiredPerformanceBoundary"
    }
}
foreach ($requiredOfficialEnvironmentBoundary in @(
    'P0R1_GIT_EXECUTABLE',
    'P0R1_OFFICIAL_PREFLIGHT',
    'AB-PERF-RUNNER-v1',
    'P0R1MeasuredSourceTreeSha256',
    'P0R1GitExecutablePath',
    'P0R1GitExecutableSha256'
)) {
    $moduleSource = Get-Content -LiteralPath $ModulePath -Raw -Encoding UTF8
    if (-not $moduleSource.Contains($requiredOfficialEnvironmentBoundary)) {
        throw "Official C# authority environment boundary is absent: $requiredOfficialEnvironmentBoundary"
    }
}
$performanceDependencyIndex = $performanceWrapper.IndexOf(
    'Assert-P0R1RepositoryDependencyContract',
    [StringComparison]::Ordinal
)
$performanceRendererIndex = $performanceWrapper.IndexOf(
    'Assert-P0R1RendererDecisionContract',
    [StringComparison]::Ordinal
)
$performanceFocusedIndex = $performanceWrapper.IndexOf(
    'Invoke-P0R1FocusedVerification',
    [StringComparison]::Ordinal
)
if ($performanceDependencyIndex -lt 0 -or
    $performanceRendererIndex -lt 0 -or
    $performanceFocusedIndex -lt 0 -or
    $performanceDependencyIndex -gt $performanceFocusedIndex -or
    $performanceRendererIndex -gt $performanceFocusedIndex) {
    throw 'Performance dependency and renderer/source contracts must run before focused build/test execution.'
}

function Assert-Equal {
    param(
        [Parameter(Mandatory = $true)]$Actual,
        [Parameter(Mandatory = $true)]$Expected,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if ($Actual -ne $Expected) {
        throw "$Message. Expected '$Expected', got '$Actual'."
    }
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string[]]$Actual,
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if ($Actual -notcontains $Expected) {
        throw "$Message. Missing '$Expected' in '$($Actual -join ' ')'."
    }
}

function Assert-ThrowsExact {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Action,
        [Parameter(Mandatory = $true)][string]$ExpectedType,
        [Parameter(Mandatory = $true)][string]$ExpectedMessage
    )

    try {
        & $Action
    }
    catch {
        Assert-Equal -Actual $_.Exception.GetType().FullName -Expected $ExpectedType -Message 'Exception type'
        Assert-Equal -Actual $_.Exception.Message -Expected $ExpectedMessage -Message 'Exception message'
        return
    }

    throw "Expected $ExpectedType with exact message '$ExpectedMessage'."
}

function Assert-ThrowsDynamicOutput {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Action,
        [Parameter(Mandatory = $true)][string]$ExpectedType,
        [Parameter(Mandatory = $true)][string]$ExpectedMessagePattern
    )

    try {
        & $Action
    }
    catch {
        Assert-Equal -Actual $_.Exception.GetType().FullName -Expected $ExpectedType -Message 'Exception type'
        if ($_.Exception.Message -notlike $ExpectedMessagePattern) {
            throw "Unexpected dynamic error. Expected '$ExpectedMessagePattern', got '$($_.Exception.Message)'."
        }
        return
    }

    throw "Expected $ExpectedType matching dynamic output '$ExpectedMessagePattern'."
}

$wrapperFailureFixtureRoot = Join-Path `
    ([IO.Path]::GetTempPath()) `
    ("p0r1-wrapper-failure-" + [guid]::NewGuid().ToString('N'))
$originalFixtureProductTests = [Environment]::GetEnvironmentVariable(
    'P0R1_FIXTURE_PRODUCT_TESTS',
    [EnvironmentVariableTarget]::Process
)
try {
    $fixtureScriptRoot = Join-Path $wrapperFailureFixtureRoot 'scripts\scatter-rendering'
    $fixturePrototypeRoot = Join-Path $wrapperFailureFixtureRoot 'prototypes\scatter-rendering'
    $fixtureTestOutputRoot = Join-Path `
        $fixturePrototypeRoot `
        'tests\AnalogBoard.ScatterRendering.Tests\bin\x64\Release\net10.0-windows'
    $null = New-Item -ItemType Directory -Path $fixtureScriptRoot -Force
    $null = New-Item -ItemType Directory -Path $fixtureTestOutputRoot -Force
    Copy-Item -LiteralPath $WrapperPath -Destination $fixtureScriptRoot
    Copy-Item -LiteralPath $PerformanceWrapperPath -Destination $fixtureScriptRoot
    Set-Content `
        -LiteralPath (Join-Path $fixtureTestOutputRoot 'AnalogBoard.ScatterRendering.Tests.dll') `
        -Value 'fixture' `
        -Encoding UTF8
    @'
function Assert-P0R1VerificationSelection {
    param($Mode, $Configuration, $Architecture)
}
function Assert-P0R1RepositoryDependencyContract {
    param($RepositoryRoot)
    return [pscustomobject]@{
        SdkVersion = '10.0.302'
        DesktopRuntimeVersion = '10.0.10'
        TargetFramework = 'net10.0-windows'
        ExternalNuGetPackageCount = 0
    }
}
function Assert-P0R1RendererDecisionContract {
    param($RepositoryRoot)
    return [pscustomobject]@{
        DecisionId = 'P0-R1-RENDERER-v1'
        SelectedCandidateId = 'fixture'
    }
}
function Invoke-P0R1FocusedVerification {
    param($RepositoryRoot, $PrototypeRoot, $Configuration, $Architecture)
    $productTestsExecuted = $env:P0R1_FIXTURE_PRODUCT_TESTS -eq '1'
    return [pscustomobject]@{
        Status = $(if ($productTestsExecuted) { 'Pass' } else { 'ContractOnly' })
        ProductTestsExecuted = $productTestsExecuted
    }
}
function Invoke-P0R1SanitizedDotNet {
    param($Arguments, $WorkingDirectory, $GitExecutablePath, [switch]$OfficialPreflight)
    if ($Arguments -contains '--version') {
        return [pscustomobject]@{ ExitCode = 0; Output = @('10.0.302') }
    }
    return [pscustomobject]@{
        ExitCode = 0
        Output = @(
            'Microsoft.NETCore.App 10.0.10 [C:\dotnet]',
            'Microsoft.WindowsDesktop.App 10.0.10 [C:\dotnet]'
        )
    }
}
function Assert-P0R1ToolchainOutput {
    param($SdkVersionLines, $RuntimeLines)
    return [pscustomobject]@{
        SdkVersion = '10.0.302'
        DesktopRuntimeVersion = '10.0.10'
    }
}
function git {
    if ($args -contains 'rev-parse') {
        $global:LASTEXITCODE = 0
        return ('a' * 40)
    }
    if ($args -contains 'status') {
        $global:LASTEXITCODE = 128
        return 'fixture git status failure'
    }
    $global:LASTEXITCODE = 0
}
Export-ModuleMember -Function *
'@ | Set-Content `
        -LiteralPath (Join-Path $fixtureScriptRoot 'scatter_verification_core.psm1') `
        -Encoding UTF8

    # Given: Focused verification reports the legacy ContractOnly state with no product tests.
    # When: The standalone mandatory verification wrapper is executed.
    $originalErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $contractOnlyOutput = @(
            & powershell.exe `
                -NoLogo `
                -NoProfile `
                -ExecutionPolicy Bypass `
                -File (Join-Path $fixtureScriptRoot 'verify.ps1') `
                -Mode Focused `
                -Configuration Release `
                -Architecture x64 2>&1 |
                ForEach-Object { $_.ToString() }
        )
        $contractOnlyExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $originalErrorActionPreference
    }
    # Then: The wrapper fails with the exact gate-honesty message and exit code.
    Assert-Equal -Actual $contractOnlyExitCode -Expected 2 -Message 'ContractOnly wrapper exit code'
    Assert-Contains `
        -Actual $contractOnlyOutput `
        -Expected 'P0-R1 verification failed: Focused verification requires the complete P0-R1 prototype; product tests were not executed.' `
        -Message 'ContractOnly wrapper failure'

    # Given: Revision lookup succeeds but Git status exits nonzero.
    # When: The dry-run performance wrapper derives source provenance.
    [Environment]::SetEnvironmentVariable(
        'P0R1_FIXTURE_PRODUCT_TESTS',
        '1',
        [EnvironmentVariableTarget]::Process
    )
    try {
        $ErrorActionPreference = 'Continue'
        $gitStatusFailureOutput = @(
            & powershell.exe `
                -NoLogo `
                -NoProfile `
                -ExecutionPolicy Bypass `
                -File (Join-Path $fixtureScriptRoot 'run_performance.ps1') `
                -Mode DryRun `
                -OutputRoot (Join-Path $wrapperFailureFixtureRoot 'output') 2>&1 |
                ForEach-Object { $_.ToString() }
        )
        $gitStatusFailureExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $originalErrorActionPreference
    }
    # Then: It fails closed instead of recording a clean worktree.
    Assert-Equal -Actual $gitStatusFailureExitCode -Expected 2 -Message 'Git status failure exit code'
    Assert-Contains `
        -Actual $gitStatusFailureOutput `
        -Expected 'P0-R1 performance execution failed: Unable to determine Git worktree status.' `
        -Message 'Git status failure'
}
finally {
    [Environment]::SetEnvironmentVariable(
        'P0R1_FIXTURE_PRODUCT_TESTS',
        $originalFixtureProductTests,
        [EnvironmentVariableTarget]::Process
    )
    Remove-Item `
        -LiteralPath $wrapperFailureFixtureRoot `
        -Recurse `
        -Force `
        -ErrorAction SilentlyContinue
}

function Copy-P0R1FixtureSourceTree {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$DestinationRoot
    )

    if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
        throw [IO.DirectoryNotFoundException]::new(
            "P0-R1 fixture source tree is absent: $SourceRoot"
        )
    }

    $trimCharacters = [char[]]@(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    $resolvedSource = (
        Resolve-Path -LiteralPath $SourceRoot -ErrorAction Stop
    ).Path.TrimEnd($trimCharacters)
    $resolvedDestination = [IO.Path]::GetFullPath(
        $DestinationRoot
    ).TrimEnd($trimCharacters)
    $pathComparison = [StringComparison]::OrdinalIgnoreCase
    $sourcePrefix = $resolvedSource + [IO.Path]::DirectorySeparatorChar
    if ($resolvedDestination.Equals($resolvedSource, $pathComparison) -or
        $resolvedDestination.StartsWith($sourcePrefix, $pathComparison)) {
        throw [InvalidOperationException]::new(
            'P0-R1 fixture destination must be outside the source tree.'
        )
    }

    $null = New-Item `
        -ItemType Directory `
        -Path $resolvedDestination `
        -Force
    $pendingDirectories = [Collections.Generic.Queue[string]]::new()
    $pendingDirectories.Enqueue($resolvedSource)
    while ($pendingDirectories.Count -gt 0) {
        $currentDirectory = $pendingDirectories.Dequeue()
        foreach ($entry in @(
            Get-ChildItem -LiteralPath $currentDirectory -Force |
                Sort-Object -Property FullName
        )) {
            if ($entry.PSIsContainer) {
                if ($entry.Name -ieq 'bin' -or $entry.Name -ieq 'obj') {
                    continue
                }
                $pendingDirectories.Enqueue($entry.FullName)
                continue
            }

            $relativePath = $entry.FullName.Substring(
                $resolvedSource.Length + 1
            )
            $destinationPath = Join-Path $resolvedDestination $relativePath
            $null = New-Item `
                -ItemType Directory `
                -Path (Split-Path -Parent $destinationPath) `
                -Force
            Copy-Item `
                -LiteralPath $entry.FullName `
                -Destination $destinationPath `
                -Force
        }
    }
}

$fixtureCopyTestRoot = Join-Path `
    ([IO.Path]::GetTempPath()) `
    ("p0r1-fixture-copy-" + [guid]::NewGuid().ToString('N'))
$fixtureCopySource = Join-Path $fixtureCopyTestRoot 'source'
$fixtureCopyDestination = Join-Path $fixtureCopyTestRoot 'destination'
try {
    $trackedDirectory = Join-Path $fixtureCopySource 'tracked'
    $generatedObjDirectory = Join-Path $fixtureCopySource 'tracked\obj\x64'
    $generatedBinDirectory = Join-Path $fixtureCopySource 'tracked\bin\x64'
    $null = New-Item -ItemType Directory -Path $trackedDirectory -Force
    $null = New-Item -ItemType Directory -Path $generatedObjDirectory -Force
    $null = New-Item -ItemType Directory -Path $generatedBinDirectory -Force
    Set-Content `
        -LiteralPath (Join-Path $trackedDirectory 'Keep.cs') `
        -Value '// tracked source' `
        -Encoding UTF8
    Set-Content `
        -LiteralPath (Join-Path $generatedObjDirectory 'Generated.cs') `
        -Value '// generated obj source' `
        -Encoding UTF8
    Set-Content `
        -LiteralPath (Join-Path $generatedBinDirectory 'Generated.dll') `
        -Value 'generated binary placeholder' `
        -Encoding UTF8

    # Given: A source fixture containing a normal file and generated bin/obj trees.
    # When: The source fixture tree is copied.
    Copy-P0R1FixtureSourceTree `
        -SourceRoot $fixtureCopySource `
        -DestinationRoot $fixtureCopyDestination

    # Then: The normal file is copied and both generated trees are excluded.
    Assert-Equal `
        -Actual (Test-Path -LiteralPath (Join-Path $fixtureCopyDestination 'tracked\Keep.cs') -PathType Leaf) `
        -Expected $true `
        -Message 'Fixture copy tracked source'
    Assert-Equal `
        -Actual (Test-Path -LiteralPath (Join-Path $fixtureCopyDestination 'tracked\obj') -PathType Container) `
        -Expected $false `
        -Message 'Fixture copy obj exclusion'
    Assert-Equal `
        -Actual (Test-Path -LiteralPath (Join-Path $fixtureCopyDestination 'tracked\bin') -PathType Container) `
        -Expected $false `
        -Message 'Fixture copy bin exclusion'

    $missingFixtureSource = Join-Path $fixtureCopyTestRoot 'missing'
    # Given: A missing source fixture root.
    # When: The source fixture tree copy is requested.
    # Then: The helper fails with the exact directory error.
    Assert-ThrowsExact -Action {
        Copy-P0R1FixtureSourceTree `
            -SourceRoot $missingFixtureSource `
            -DestinationRoot (Join-Path $fixtureCopyTestRoot 'missing-destination')
    } -ExpectedType 'System.IO.DirectoryNotFoundException' -ExpectedMessage "P0-R1 fixture source tree is absent: $missingFixtureSource"

    $nestedDestination = Join-Path $fixtureCopySource 'nested-destination'
    # Given: A destination nested inside the source fixture root.
    # When: The source fixture tree copy is requested.
    # Then: The helper rejects recursive self-copy before creating the destination.
    Assert-ThrowsExact -Action {
        Copy-P0R1FixtureSourceTree `
            -SourceRoot $fixtureCopySource `
            -DestinationRoot $nestedDestination
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 fixture destination must be outside the source tree.'
}
finally {
    Remove-Item `
        -LiteralPath $fixtureCopyTestRoot `
        -Recurse `
        -Force `
        -ErrorAction SilentlyContinue
}

$canonicalProfileRelativePath =
    'docs/reference/scatter-rendering/phase0/performance-reference-profile-v1.json'
$canonicalProfilePath = Join-Path $RepositoryRoot $canonicalProfileRelativePath
if (-not (Test-Path -LiteralPath $canonicalProfilePath -PathType Leaf)) {
    throw "RED: owner-approved canonical performance profile is absent: $canonicalProfileRelativePath"
}

# Given: The owner-approved Dell observation at the only canonical profile path.
$canonicalProfileContract =
    Assert-P0R1CanonicalReferenceProfileContract -RepositoryRoot $RepositoryRoot
$expectedCanonicalProfile = [ordered]@{
    schema_id = 'analogboard.scatter-rendering.reference-profile.v1'
    profile_id = 'AB-PERF-REF-v1'
    profile_status = 'owner_pinned'
    owner_approval_id = 'P0-R1-AB-PERF-REF-v1-20260723'
    manufacturer = 'Dell Inc.'
    model = 'Precision 3680'
    machine_name = 'ANALYZER_S1'
    os_product = 'Microsoft Windows 11 Pro'
    os_version = '10.0.26200'
    os_build = '26200'
    cpu = 'Intel(R) Core(TM) i9-14900'
    ram_bytes = [int64]68390989824
    gpu_name = 'Intel(R) UHD Graphics 770 | NVIDIA RTX 4000 Ada Generation'
    gpu_driver_version = '32.0.101.7085 | 32.0.15.9595'
    display_width = [int64]1920
    display_height = [int64]1080
    display_refresh_hz = [int64]60
    display_dpi_x = [int64]96
    display_dpi_y = [int64]96
    power_scheme_guid = '381b4222-f694-41f0-9685-ff5bb260df2e'
    storage_model = 'NVMe PC SN820 NVMe WD 4096GB'
    storage_serial = 'E823_8FA6_BF53_0001_001B_444A_4100_7672.'
    storage_bus_type = 'SCSI'
    monotonic_clock = 'System.Diagnostics.Stopwatch'
    stopwatch_frequency = [int64]10000000
    sdk_version = '10.0.302'
    desktop_runtime_version = '10.0.10'
    target_framework = 'net10.0-windows'
    configuration = 'Release'
    architecture = 'x64'
    remote_session = $false
}

# When: The canonical file is parsed through the production contract.
$actualCanonicalFields = @($canonicalProfileContract.Profile.PSObject.Properties.Name)

# Then: Every approved identity field, status, and approval ID is exact.
Assert-Equal `
    -Actual $actualCanonicalFields.Count `
    -Expected $expectedCanonicalProfile.Count `
    -Message 'Canonical profile field count'
foreach ($fieldName in $expectedCanonicalProfile.Keys) {
    Assert-Contains `
        -Actual $actualCanonicalFields `
        -Expected $fieldName `
        -Message 'Canonical profile field set'
    if ($canonicalProfileContract.Profile.$fieldName -cne $expectedCanonicalProfile[$fieldName]) {
        throw "Canonical profile field mismatch: $fieldName."
    }
}

$profileDriftRoot =
    Join-Path ([IO.Path]::GetTempPath()) ("p0r1-profile-drift-" + [guid]::NewGuid().ToString('N'))
try {
    $profileDriftPath = Join-Path $profileDriftRoot $canonicalProfileRelativePath
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $profileDriftPath) -Force
    Copy-Item -LiteralPath $canonicalProfilePath -Destination $profileDriftPath

    # Given: The canonical identity is accidentally returned to live-observation status.
    (Get-Content -LiteralPath $profileDriftPath -Raw -Encoding UTF8).Replace(
        '"profile_status": "owner_pinned"',
        '"profile_status": "live_observation"'
    ) | Set-Content -LiteralPath $profileDriftPath -Encoding UTF8

    # When/Then: The production contract rejects it with the exact status field.
    Assert-ThrowsExact -Action {
        Assert-P0R1CanonicalReferenceProfileContract -RepositoryRoot $profileDriftRoot
    } -ExpectedType 'System.InvalidOperationException' `
        -ExpectedMessage 'P0-R1 canonical reference profile field mismatch: profile_status.'

    Copy-Item -LiteralPath $canonicalProfilePath -Destination $profileDriftPath -Force

    # Given: One approved hardware identity value drifts.
    (Get-Content -LiteralPath $profileDriftPath -Raw -Encoding UTF8).Replace(
        '"gpu_driver_version": "32.0.101.7085 | 32.0.15.9595"',
        '"gpu_driver_version": "32.0.101.7085 | 32.0.15.9999"'
    ) | Set-Content -LiteralPath $profileDriftPath -Encoding UTF8

    # When/Then: The production contract names the drifting identity field.
    Assert-ThrowsExact -Action {
        Assert-P0R1CanonicalReferenceProfileContract -RepositoryRoot $profileDriftRoot
    } -ExpectedType 'System.InvalidOperationException' `
        -ExpectedMessage 'P0-R1 canonical reference profile field mismatch: gpu_driver_version.'
}
finally {
    Remove-Item -LiteralPath $profileDriftRoot -Recurse -Force -ErrorAction SilentlyContinue
}

# Given: The bounded summary of the sealed Dell Official suite.
$officialEvidence =
    Assert-P0R1OfficialPerformanceEvidenceContract -RepositoryRoot $RepositoryRoot

# When/Then: Its immutable suite identity and exact eight-run boundary validate.
Assert-Equal `
    -Actual $officialEvidence.EvidenceId `
    -Expected 'P0-R1-OFFICIAL-PERFORMANCE-v1' `
    -Message 'Official performance evidence identity'
Assert-Equal `
    -Actual $officialEvidence.ManifestSha256 `
    -Expected 'b175b532f99ada8d1da8ad58b52b29613f594c79d8e1fd9de4234e98c0beb729' `
    -Message 'Official performance manifest identity'
Assert-Equal `
    -Actual $officialEvidence.RunCount `
    -Expected 8 `
    -Message 'Official performance run count'
Assert-Equal `
    -Actual $officialEvidence.OfficialAcceptance `
    -Expected $true `
    -Message 'Official performance acceptance'

$officialEvidenceRelativePath =
    'docs/reference/scatter-rendering/phase0/official-performance-evidence-v1.json'
$officialEvidencePath = Join-Path $RepositoryRoot $officialEvidenceRelativePath
$officialEvidenceDriftRoot =
    Join-Path ([IO.Path]::GetTempPath()) ("p0r1-official-evidence-drift-" + [guid]::NewGuid().ToString('N'))
try {
    foreach ($relativePath in @(
        $canonicalProfileRelativePath,
        $officialEvidenceRelativePath
    )) {
        $destination = Join-Path $officialEvidenceDriftRoot $relativePath
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
        Copy-Item -LiteralPath (Join-Path $RepositoryRoot $relativePath) -Destination $destination
    }
    $driftPath = Join-Path $officialEvidenceDriftRoot $officialEvidenceRelativePath

    # Given: The sealed source revision drifts from the audited Official HEAD.
    (Get-Content -LiteralPath $driftPath -Raw -Encoding UTF8).Replace(
        '5937a113755784acda5ec03ad4e254f9f881b885',
        '4937a113755784acda5ec03ad4e254f9f881b885'
    ) | Set-Content -LiteralPath $driftPath -Encoding UTF8
    # When/Then: The production contract names the source identity.
    Assert-ThrowsExact -Action {
        Assert-P0R1OfficialPerformanceEvidenceContract -RepositoryRoot $officialEvidenceDriftRoot
    } -ExpectedType 'System.InvalidOperationException' `
        -ExpectedMessage 'P0-R1 Official evidence session identity mismatch: source_revision.'

    Copy-Item -LiteralPath $officialEvidencePath -Destination $driftPath -Force

    # Given: The suite is relabelled as non-official.
    (Get-Content -LiteralPath $driftPath -Raw -Encoding UTF8).Replace(
        '"official_acceptance": true',
        '"official_acceptance": false'
    ) | Set-Content -LiteralPath $driftPath -Encoding UTF8
    # When/Then: Official acceptance fails closed.
    Assert-ThrowsExact -Action {
        Assert-P0R1OfficialPerformanceEvidenceContract -RepositoryRoot $officialEvidenceDriftRoot
    } -ExpectedType 'System.InvalidOperationException' `
        -ExpectedMessage 'P0-R1 Official evidence must be official, non-development, and not a production throughput guarantee.'

    Copy-Item -LiteralPath $officialEvidencePath -Destination $driftPath -Force

    # Given: One raw artifact seal is changed without changing the run identity.
    (Get-Content -LiteralPath $driftPath -Raw -Encoding UTF8).Replace(
        'fbd1066ff885cdb3b7765ce1d895180666c882f492a0377c47dc405b29eff74e',
        '0bd1066ff885cdb3b7765ce1d895180666c882f492a0377c47dc405b29eff74e'
    ) | Set-Content -LiteralPath $driftPath -Encoding UTF8
    # When/Then: The exact run seal fails closed.
    Assert-ThrowsExact -Action {
        Assert-P0R1OfficialPerformanceEvidenceContract -RepositoryRoot $officialEvidenceDriftRoot
    } -ExpectedType 'System.InvalidOperationException' `
        -ExpectedMessage 'P0-R1 Official evidence run identity mismatch at index 0: sha256.'

    Copy-Item -LiteralPath $officialEvidencePath -Destination $driftPath -Force

    # Given: The observation-only headroom run is promoted into a hard pass.
    (Get-Content -LiteralPath $driftPath -Raw -Encoding UTF8).Replace(
        '"verdict": "observed"',
        '"verdict": "pass"'
    ) | Set-Content -LiteralPath $driftPath -Encoding UTF8
    # When/Then: Hard/headroom separation is immutable.
    Assert-ThrowsExact -Action {
        Assert-P0R1OfficialPerformanceEvidenceContract -RepositoryRoot $officialEvidenceDriftRoot
    } -ExpectedType 'System.InvalidOperationException' `
        -ExpectedMessage 'P0-R1 Official evidence run identity mismatch at index 7: verdict.'

    Copy-Item -LiteralPath $officialEvidencePath -Destination $driftPath -Force

    # Given: A duplicate acceptance property attempts to shadow the true value.
    $duplicateEvidence = (Get-Content -LiteralPath $driftPath -Raw -Encoding UTF8).Replace(
        '"official_acceptance": true,',
        '"official_acceptance": true, "official_acceptance": false,'
    )
    Set-Content -LiteralPath $driftPath -Value $duplicateEvidence -Encoding UTF8
    # When/Then: Duplicate JSON properties are rejected before conversion.
    Assert-ThrowsExact -Action {
        Assert-P0R1OfficialPerformanceEvidenceContract -RepositoryRoot $officialEvidenceDriftRoot
    } -ExpectedType 'System.InvalidOperationException' `
        -ExpectedMessage 'P0-R1 Official performance evidence must not contain duplicate JSON property names: official_acceptance.'
}
finally {
    Remove-Item `
        -LiteralPath $officialEvidenceDriftRoot `
        -Recurse `
        -Force `
        -ErrorAction SilentlyContinue
}

function Set-FixtureSolution {
    param(
        [Parameter(Mandatory = $true)][string]$PrototypeRoot,
        [string[]]$Projects = @('Core', 'Wpf', 'Tests'),
        [switch]$IncludeDebug,
        [switch]$IncludeUnexpected
    )

    $catalog = @{
        Core = @{
            Name = 'AnalogBoard.ScatterRendering.Core'
            Path = 'src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj'
            Guid = '{5BD7B476-EF87-4F58-8CC4-F59D1110DD00}'
        }
        Wpf = @{
            Name = 'AnalogBoard.ScatterRendering.Wpf'
            Path = 'src\AnalogBoard.ScatterRendering.Wpf\AnalogBoard.ScatterRendering.Wpf.csproj'
            Guid = '{F5A0A84B-2AFE-43D7-BC3C-D7597CFD7B7E}'
        }
        Tests = @{
            Name = 'AnalogBoard.ScatterRendering.Tests'
            Path = 'tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj'
            Guid = '{83C17C7C-D260-48EA-9D55-66EF504D3BAF}'
        }
        Unexpected = @{
            Name = 'Unexpected.Project'
            Path = 'src\Unexpected\Unexpected.csproj'
            Guid = '{11111111-1111-1111-1111-111111111111}'
        }
    }
    $selected = @($Projects)
    if ($IncludeUnexpected) {
        $selected += 'Unexpected'
    }

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('Microsoft Visual Studio Solution File, Format Version 12.00')
    $lines.Add('# Visual Studio Version 17')
    $lines.Add('VisualStudioVersion = 17.0.31903.59')
    $lines.Add('MinimumVisualStudioVersion = 10.0.40219.1')
    foreach ($key in $selected) {
        $project = $catalog[$key]
        $lines.Add("Project(`"{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}`") = `"$($project.Name)`", `"$($project.Path)`", `"$($project.Guid)`"")
        $lines.Add('EndProject')
    }
    $lines.Add('Global')
    $lines.Add("`tGlobalSection(SolutionConfigurationPlatforms) = preSolution")
    if ($IncludeDebug) {
        $lines.Add("`t`tDebug|x64 = Debug|x64")
    }
    $lines.Add("`t`tRelease|x64 = Release|x64")
    $lines.Add("`tEndGlobalSection")
    $lines.Add("`tGlobalSection(ProjectConfigurationPlatforms) = postSolution")
    foreach ($key in $selected) {
        $guid = $catalog[$key].Guid
        if ($IncludeDebug) {
            $lines.Add("`t`t$guid.Debug|x64.ActiveCfg = Debug|x64")
            $lines.Add("`t`t$guid.Debug|x64.Build.0 = Debug|x64")
        }
        $lines.Add("`t`t$guid.Release|x64.ActiveCfg = Release|x64")
        $lines.Add("`t`t$guid.Release|x64.Build.0 = Release|x64")
    }
    $lines.Add("`tEndGlobalSection")
    $lines.Add('EndGlobal')
    $lines | Set-Content -LiteralPath (Join-Path $PrototypeRoot 'AnalogBoard.ScatterRenderingPrototype.sln') -Encoding UTF8
}

function New-ContractFixture {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [ValidateSet('Empty', 'CoreOnly', 'Complete')]
        [string]$Shape = 'Complete',
        [switch]$IncludeDebug
    )

    $prototypeRoot = Join-Path $Root 'prototypes\scatter-rendering'
    New-Item -ItemType Directory -Force -Path $prototypeRoot | Out-Null
    @'
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources><clear /></packageSources>
  <fallbackPackageFolders><clear /></fallbackPackageFolders>
  <auditSources><clear /></auditSources>
</configuration>
'@ | Set-Content -LiteralPath (Join-Path $prototypeRoot 'NuGet.Config') -Encoding UTF8

    if ($Shape -eq 'Empty') {
        return $prototypeRoot
    }

    $coreRoot = Join-Path $prototypeRoot 'src\AnalogBoard.ScatterRendering.Core'
    New-Item -ItemType Directory -Force -Path $coreRoot | Out-Null
    @'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><TargetFramework>net10.0-windows</TargetFramework><PlatformTarget>x64</PlatformTarget></PropertyGroup>
</Project>
'@ | Set-Content -LiteralPath (Join-Path $coreRoot 'AnalogBoard.ScatterRendering.Core.csproj') -Encoding UTF8

    if ($Shape -eq 'CoreOnly') {
        Set-FixtureSolution -PrototypeRoot $prototypeRoot -Projects @('Core')
        return $prototypeRoot
    }

    $wpfRoot = Join-Path $prototypeRoot 'src\AnalogBoard.ScatterRendering.Wpf'
    $resourceRoot = Join-Path $wpfRoot 'Properties'
    $testsRoot = Join-Path $prototypeRoot 'tests\AnalogBoard.ScatterRendering.Tests'
    New-Item -ItemType Directory -Force -Path $resourceRoot, $testsRoot | Out-Null
    @'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Library</OutputType>
    <TargetFramework>net10.0-windows</TargetFramework>
    <PlatformTarget>x64</PlatformTarget>
    <UseWPF>true</UseWPF>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="..\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj" />
  </ItemGroup>
</Project>
'@ | Set-Content -LiteralPath (Join-Path $wpfRoot 'AnalogBoard.ScatterRendering.Wpf.csproj') -Encoding UTF8
    @'
<root>
  <data name="HarnessCopyBufferLengthMismatch"><value>copy</value></data>
  <data name="HarnessFrameShapeMismatch"><value>shape</value></data>
  <data name="HarnessOwnerDispatcherRequired"><value>dispatcher</value></data>
  <data name="RequiredPrototypeResourceAbsent"><value>missing {0}</value></data>
  <data name="SurfaceBufferLengthMismatch"><value>buffer</value></data>
  <data name="SurfaceDisposed"><value>disposed</value></data>
  <data name="SurfaceGenerationNotIncreasing"><value>generation</value></data>
  <data name="SurfaceHeightOutOfRange"><value>height</value></data>
  <data name="SurfaceOwnerMustBeSta"><value>sta</value></data>
  <data name="SurfaceWidthOutOfRange"><value>width</value></data>
  <data name="SurfaceWrongThread"><value>thread</value></data>
</root>
'@ | Set-Content -LiteralPath (Join-Path $resourceRoot 'Resources.resx') -Encoding UTF8
    @'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0-windows</TargetFramework>
    <PlatformTarget>x64</PlatformTarget>
    <UseWPF>true</UseWPF>
    <RuntimeFrameworkVersion>10.0.10</RuntimeFrameworkVersion>
    <RollForward>Disable</RollForward>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="..\..\src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj" />
    <ProjectReference Include="..\..\src\AnalogBoard.ScatterRendering.Wpf\AnalogBoard.ScatterRendering.Wpf.csproj" />
  </ItemGroup>
</Project>
'@ | Set-Content -LiteralPath (Join-Path $testsRoot 'AnalogBoard.ScatterRendering.Tests.csproj') -Encoding UTF8
    Set-FixtureSolution -PrototypeRoot $prototypeRoot -IncludeDebug:$IncludeDebug
    return $prototypeRoot
}

function Add-FixtureXml {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectPath,
        [Parameter(Mandatory = $true)][string]$Fragment
    )

    $source = Get-Content -LiteralPath $ProjectPath -Raw -Encoding UTF8
    $source.Replace('</Project>', "$Fragment`r`n</Project>") |
        Set-Content -LiteralPath $ProjectPath -Encoding UTF8
}

$null = Assert-P0R1RepositoryDependencyContract -RepositoryRoot $RepositoryRoot
$responseFileRelativePath = 'prototypes/scatter-rendering/Directory.Build.rsp'
$null = & git -C $RepositoryRoot check-ignore -q -- $responseFileRelativePath
if ($LASTEXITCODE -eq 0) {
    throw "P0-R1 tracked build response file must not be ignored: $responseFileRelativePath."
}
if ($LASTEXITCODE -ne 1) {
    throw "git check-ignore failed while validating $responseFileRelativePath with exit code $LASTEXITCODE."
}
$ambientEnvironment = [ordered]@{
    SystemRoot = 'C:\Windows'
    TEMP = 'C:\Temp'
    Optimize = 'false'
    DefineConstants = 'AMBIENT_DEFINE'
    DirectoryBuildTargetsPath = 'C:\outside\Directory.Build.targets'
    DOTNET_STARTUP_HOOKS = 'C:\outside\hook.dll'
    DOTNET_NOLOGO = '0'
    COMPlus_TieredCompilation = '0'
}
$sanitizedEnvironment = Get-P0R1SanitizedDotNetEnvironment -Environment $ambientEnvironment
Assert-Equal -Actual $sanitizedEnvironment['SystemRoot'] -Expected 'C:\Windows' -Message 'Sanitized environment retains SystemRoot'
Assert-Equal -Actual $sanitizedEnvironment['TEMP'] -Expected 'C:\Temp' -Message 'Sanitized environment retains TEMP'
Assert-Equal -Actual $sanitizedEnvironment['DOTNET_CLI_TELEMETRY_OPTOUT'] -Expected '1' -Message 'Sanitized environment pins telemetry opt-out'
Assert-Equal -Actual $sanitizedEnvironment['DOTNET_NOLOGO'] -Expected '1' -Message 'Sanitized environment pins no-logo'
Assert-Equal -Actual $sanitizedEnvironment['MSBUILDDISABLENODEREUSE'] -Expected '1' -Message 'Sanitized environment disables MSBuild node reuse'
Assert-Equal -Actual $sanitizedEnvironment['DOTNET_CLI_USE_MSBUILD_SERVER'] -Expected '0' -Message 'Sanitized environment disables the MSBuild server'
foreach ($forbiddenName in @(
    'Optimize',
    'DefineConstants',
    'DirectoryBuildTargetsPath',
    'DOTNET_STARTUP_HOOKS',
    'COMPlus_TieredCompilation'
)) {
    Assert-Equal -Actual $sanitizedEnvironment.ContainsKey($forbiddenName) -Expected $false -Message "Sanitized environment removes $forbiddenName"
}
$installedLicense = Assert-P0R1DependencyManifestContract `
    -ManifestPath (Join-Path $RepositoryRoot 'docs\dependencies\analogboard-p0-r1-dependencies.json')
Assert-Equal -Actual $installedLicense.PrimaryInstalledPath -Expected 'C:\Program Files\dotnet\LICENSE.txt' -Message 'Primary license path'
Assert-Equal -Actual $installedLicense.PrimarySizeBytes -Expected 9519L -Message 'Primary license size'
Assert-Equal -Actual $installedLicense.ThirdPartyNoticesSizeBytes -Expected 78887L -Message 'Third-party notices size'

# Given: The only supported P0-R1 verification tuple.
# When: The selection is validated.
# Then: Focused / Release / x64 passes unchanged.
$selection = Assert-P0R1VerificationSelection -Mode 'Focused' -Configuration 'Release' -Architecture 'x64'
Assert-Equal -Actual $selection.Mode -Expected 'Focused' -Message 'Mode selection'
Assert-Equal -Actual $selection.Configuration -Expected 'Release' -Message 'Configuration selection'
Assert-Equal -Actual $selection.Architecture -Expected 'x64' -Message 'Architecture selection'

# Given: Empty and unsupported selection values.
# When/Then: Every value fails with the exact ArgumentException contract.
$invalidSelections = @(
    @{ Mode = ''; Configuration = 'Release'; Architecture = 'x64'; Parameter = 'Mode'; Value = '' },
    @{ Mode = 'Phase'; Configuration = 'Release'; Architecture = 'x64'; Parameter = 'Mode'; Value = 'Phase' },
    @{ Mode = 'Focused'; Configuration = ''; Architecture = 'x64'; Parameter = 'Configuration'; Value = '' },
    @{ Mode = 'Focused'; Configuration = 'Debug'; Architecture = 'x64'; Parameter = 'Configuration'; Value = 'Debug' },
    @{ Mode = 'Focused'; Configuration = 'Release'; Architecture = ''; Parameter = 'Architecture'; Value = '' },
    @{ Mode = 'Focused'; Configuration = 'Release'; Architecture = 'arm64'; Parameter = 'Architecture'; Value = 'arm64' }
)
foreach ($case in $invalidSelections) {
    $expected = [ArgumentException]::new(
        "$($case.Parameter) must be exactly '$(@{ Mode = 'Focused'; Configuration = 'Release'; Architecture = 'x64' }[$case.Parameter])'; got '$($case.Value)'.",
        $case.Parameter
    ).Message
    Assert-ThrowsExact -Action {
        Assert-P0R1VerificationSelection `
            -Mode $case.Mode `
            -Configuration $case.Configuration `
            -Architecture $case.Architecture
    } -ExpectedType 'System.ArgumentException' -ExpectedMessage $expected
}

# Given: The exact SDK and Desktop Runtime inventory.
# When: Toolchain output is validated.
# Then: Exact versions are returned without roll-forward.
$toolchain = Assert-P0R1ToolchainOutput `
    -SdkVersionLines @('10.0.302') `
    -RuntimeLines @('Microsoft.NETCore.App 10.0.10 [C:\dotnet]', 'Microsoft.WindowsDesktop.App 10.0.10 [C:\dotnet]')
Assert-Equal -Actual $toolchain.SdkVersion -Expected '10.0.302' -Message 'SDK version'
Assert-Equal -Actual $toolchain.DesktopRuntimeVersion -Expected '10.0.10' -Message 'Desktop Runtime version'

# Given: SDK/runtime versions one patch away and a missing runtime.
# When/Then: Each substitution uses an exact InvalidOperationException contract.
$toolchainFailures = @(
    @{ Sdk = @('10.0.301'); Runtime = @('Microsoft.WindowsDesktop.App 10.0.10 [C:\dotnet]'); Expected = "Exact .NET SDK 10.0.302 is required; dotnet --version returned '10.0.301'." },
    @{ Sdk = @('10.0.303'); Runtime = @('Microsoft.WindowsDesktop.App 10.0.10 [C:\dotnet]'); Expected = "Exact .NET SDK 10.0.302 is required; dotnet --version returned '10.0.303'." },
    @{ Sdk = @('10.0.302'); Runtime = @('Microsoft.WindowsDesktop.App 10.0.9 [C:\dotnet]'); Expected = 'Exact .NET Desktop Runtime 10.0.10 is required; matching entries: 0.' },
    @{ Sdk = @('10.0.302'); Runtime = @(); Expected = 'Exact .NET Desktop Runtime 10.0.10 is required; matching entries: 0.' }
)
foreach ($case in $toolchainFailures) {
    Assert-ThrowsExact -Action {
        Assert-P0R1ToolchainOutput -SdkVersionLines $case.Sdk -RuntimeLines $case.Runtime
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage $case.Expected
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ('analogboard-p0-r1-contract-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
try {
    # Given: A manifest whose installed primary license size differs by one byte.
    # When/Then: The immutable installed-license evidence fails exactly.
    $invalidManifestPath = Join-Path $temporaryRoot 'invalid-dependencies.json'
    $invalidManifest = Get-Content `
        -LiteralPath (Join-Path $RepositoryRoot 'docs\dependencies\analogboard-p0-r1-dependencies.json') `
        -Raw `
        -Encoding UTF8 | ConvertFrom-Json
    $invalidManifest.offline_sdk.license.primary.size_bytes = 9520
    $invalidManifest | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath $invalidManifestPath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Assert-P0R1DependencyManifestContract -ManifestPath $invalidManifestPath
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 dependency manifest primary installed license evidence mismatch.'

    # Given: One drift from each non-license manifest pin group.
    # When/Then: Offline identity, renderer, restore, and execution pins fail loud.
    $manifestDrifts = @(
        @{
            Name = 'offline-url'
            Mutate = { param($value) $value.offline_sdk.url = 'https://example.invalid/sdk.zip' }
            Expected = 'P0-R1 offline SDK identity mismatch.'
        },
        @{
            Name = 'renderer-policy'
            Mutate = { param($value) $value.renderer.raster_buffer_policy = 'per-frame' }
            Expected = 'P0-R1 renderer dependency contract mismatch.'
        },
        @{
            Name = 'restore-network'
            Mutate = { param($value) $value.restore.network_enabled = $true }
            Expected = 'P0-R1 restore isolation manifest contract mismatch.'
        },
        @{
            Name = 'toolchain-architecture'
            Mutate = { param($value) $value.toolchain.architecture = 'arm64' }
            Expected = 'P0-R1 toolchain execution contract mismatch.'
        }
    )
    foreach ($case in $manifestDrifts) {
        $driftPath = Join-Path $temporaryRoot ("manifest-$($case.Name).json")
        $drift = Get-Content `
            -LiteralPath (Join-Path $RepositoryRoot 'docs\dependencies\analogboard-p0-r1-dependencies.json') `
            -Raw `
            -Encoding UTF8 | ConvertFrom-Json
        & $case.Mutate $drift
        $drift | ConvertTo-Json -Depth 10 |
            Set-Content -LiteralPath $driftPath -Encoding UTF8
        Assert-ThrowsExact -Action {
            Assert-P0R1DependencyManifestContract -ManifestPath $driftPath
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage $case.Expected
    }

    # Given: Zero projects and no solution.
    # When: The scaffold state is inspected.
    # Then: Pre-scaffold Batch 1 remains an explicit contract-only state.
    $emptyPrototype = New-ContractFixture -Root (Join-Path $temporaryRoot 'empty') -Shape Empty
    $null = Assert-P0R1RestoreIsolation -PrototypeRoot $emptyPrototype
    $emptyState = Get-P0R1PrototypeState -PrototypeRoot $emptyPrototype
    Assert-Equal -Actual $emptyState.Status -Expected 'ContractOnly' -Message 'Zero-project state'
    Assert-Equal -Actual $emptyState.ProjectCount -Expected 0 -Message 'Zero-project count'

    # Given: One of the required three projects.
    # When/Then: A partial scaffold is rejected with the exact count.
    $oneProject = New-ContractFixture -Root (Join-Path $temporaryRoot 'one') -Shape CoreOnly
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $oneProject
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 prototype must contain exactly 3 projects; found 1.'

    # Given: The exact Core/WPF/Tests projects, references, resources, and Release|x64 solution.
    # When: Dependency and scaffold contracts are inspected.
    # Then: The prototype is complete with three projects.
    $completePrototype = New-ContractFixture -Root (Join-Path $temporaryRoot 'complete')
    $null = Assert-P0R1RestoreIsolation -PrototypeRoot $completePrototype
    $completeState = Get-P0R1PrototypeState -PrototypeRoot $completePrototype
    Assert-Equal -Actual $completeState.Status -Expected 'Complete' -Message 'Complete scaffold state'
    Assert-Equal -Actual $completeState.ProjectCount -Expected 3 -Message 'Complete project count'

    # Given: A fourth project file outside the fixed graph.
    # When/Then: The upper project-count boundary is rejected exactly.
    $fourProjects = New-ContractFixture -Root (Join-Path $temporaryRoot 'four')
    $extraRoot = Join-Path $fourProjects 'src\Unexpected'
    New-Item -ItemType Directory -Path $extraRoot | Out-Null
    '<Project Sdk="Microsoft.NET.Sdk" />' |
        Set-Content -LiteralPath (Join-Path $extraRoot 'Unexpected.csproj') -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $fourProjects
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 prototype must contain exactly 3 projects; found 4.'

    # Given: Three files but a solution missing WPF membership.
    # When/Then: Membership fails rather than silently building a subset.
    $missingMember = New-ContractFixture -Root (Join-Path $temporaryRoot 'missing-member')
    Set-FixtureSolution -PrototypeRoot $missingMember -Projects @('Core', 'Tests')
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $missingMember
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 solution project membership mismatch. Missing: AnalogBoard.ScatterRendering.Wpf. Unexpected: none.'

    # Given: The exact members plus an unexpected solution member.
    # When/Then: Unexpected membership is rejected exactly.
    $unexpectedMember = New-ContractFixture -Root (Join-Path $temporaryRoot 'unexpected-member')
    Set-FixtureSolution -PrototypeRoot $unexpectedMember -IncludeUnexpected
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $unexpectedMember
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 solution project membership mismatch. Missing: none. Unexpected: Unexpected.Project.'

    # Given: An exact project name/path whose declaration GUID differs from its mappings.
    # When/Then: The project tuple is rejected before mapping validation.
    $guidMismatch = New-ContractFixture -Root (Join-Path $temporaryRoot 'guid-mismatch')
    $guidMismatchPath = Join-Path $guidMismatch 'AnalogBoard.ScatterRenderingPrototype.sln'
    $guidMismatchSource = Get-Content -LiteralPath $guidMismatchPath -Raw -Encoding UTF8
    $guidMismatchSource = $guidMismatchSource.Replace(
        '"src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj", "{5BD7B476-EF87-4F58-8CC4-F59D1110DD00}"',
        '"src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj", "{11111111-1111-1111-1111-111111111111}"'
    )
    Set-Content -LiteralPath $guidMismatchPath -Value $guidMismatchSource -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $guidMismatch
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "P0-R1 solution project GUID mismatch for 'AnalogBoard.ScatterRendering.Core': '{11111111-1111-1111-1111-111111111111}'."

    # Given: A solution exposing Debug|x64 in addition to Release|x64.
    # When/Then: The extra configuration and mappings are rejected.
    $debugSolution = New-ContractFixture -Root (Join-Path $temporaryRoot 'debug') -IncludeDebug
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $debugSolution
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 solution must expose Release|x64 only. Actual: Debug|x64, Release|x64.'

    # Given: A Release solution with one missing or redirected project build mapping.
    # When/Then: Every project must have exact Release|x64 ActiveCfg and Build.0 mappings.
    foreach ($mappingMutation in @('MissingBuild', 'DebugActive')) {
        $mappingFixture = New-ContractFixture -Root (Join-Path $temporaryRoot ("mapping-$mappingMutation"))
        $mappingPath = Join-Path $mappingFixture 'AnalogBoard.ScatterRenderingPrototype.sln'
        $mappingSource = Get-Content -LiteralPath $mappingPath -Raw -Encoding UTF8
        if ($mappingMutation -eq 'MissingBuild') {
            $mappingSource = $mappingSource.Replace(
                "`t`t{5BD7B476-EF87-4F58-8CC4-F59D1110DD00}.Release|x64.Build.0 = Release|x64`r`n",
                ''
            )
        }
        else {
            $mappingSource = $mappingSource.Replace(
                '{5BD7B476-EF87-4F58-8CC4-F59D1110DD00}.Release|x64.ActiveCfg = Release|x64',
                '{5BD7B476-EF87-4F58-8CC4-F59D1110DD00}.Release|x64.ActiveCfg = Debug|x64'
            )
        }
        Set-Content -LiteralPath $mappingPath -Value $mappingSource -Encoding UTF8
        Assert-ThrowsExact -Action {
            Get-P0R1PrototypeState -PrototypeRoot $mappingFixture
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 solution must contain exact Release|x64 ActiveCfg and Build.0 mappings for all three projects.'
    }

    # Given: Invalid WPF metadata or a missing resource file.
    # When/Then: Each WPF scaffold violation has an exact actionable message.
    $wpfFalse = New-ContractFixture -Root (Join-Path $temporaryRoot 'wpf-false')
    $wpfFalsePath = Join-Path $wpfFalse 'src\AnalogBoard.ScatterRendering.Wpf\AnalogBoard.ScatterRendering.Wpf.csproj'
    (Get-Content -LiteralPath $wpfFalsePath -Raw -Encoding UTF8).Replace('<UseWPF>true</UseWPF>', '<UseWPF>false</UseWPF>') |
        Set-Content -LiteralPath $wpfFalsePath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $wpfFalse
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "P0-R1 WPF project must set UseWPF=true and OutputType=Library."

    $wpfExe = New-ContractFixture -Root (Join-Path $temporaryRoot 'wpf-exe')
    $wpfExePath = Join-Path $wpfExe 'src\AnalogBoard.ScatterRendering.Wpf\AnalogBoard.ScatterRendering.Wpf.csproj'
    (Get-Content -LiteralPath $wpfExePath -Raw -Encoding UTF8).Replace('<OutputType>Library</OutputType>', '<OutputType>Exe</OutputType>') |
        Set-Content -LiteralPath $wpfExePath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $wpfExe
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "P0-R1 WPF project must set UseWPF=true and OutputType=Library."

    $missingResource = New-ContractFixture -Root (Join-Path $temporaryRoot 'missing-resource')
    Remove-Item -LiteralPath (Join-Path $missingResource 'src\AnalogBoard.ScatterRendering.Wpf\Properties\Resources.resx')
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $missingResource
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "P0-R1 WPF resource is absent: src/AnalogBoard.ScatterRendering.Wpf/Properties/Resources.resx."

    $missingResourceKey = New-ContractFixture -Root (Join-Path $temporaryRoot 'missing-resource-key')
    $missingResourceKeyPath = Join-Path $missingResourceKey 'src\AnalogBoard.ScatterRendering.Wpf\Properties\Resources.resx'
    [xml]$missingResourceKeyXml = Get-Content -LiteralPath $missingResourceKeyPath -Raw -Encoding UTF8
    $missingResourceKeyNode = $missingResourceKeyXml.SelectSingleNode("/root/data[@name='HarnessOwnerDispatcherRequired']")
    $null = $missingResourceKeyNode.ParentNode.RemoveChild($missingResourceKeyNode)
    $missingResourceKeyXml.Save($missingResourceKeyPath)
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $missingResourceKey
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 WPF resource key is absent: HarnessOwnerDispatcherRequired.'

    $messageBoxSource = New-ContractFixture -Root (Join-Path $temporaryRoot 'message-box-source')
    $messageBoxSourcePath = Join-Path $messageBoxSource 'src\AnalogBoard.ScatterRendering.Wpf\Forbidden.cs'
    'internal static class Forbidden { internal static void Show() => System.Windows.MessageBox.Show("blocked"); }' |
        Set-Content -LiteralPath $messageBoxSourcePath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $messageBoxSource
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'MessageBox is forbidden in P0-R1 WPF source: src/AnalogBoard.ScatterRendering.Wpf/Forbidden.cs.'

    # Given: A test project without WPF support or not the required self-hosted executable.
    # When/Then: UseWPF and OutputType must be exact for direct surface tests.
    $testsLibrary = New-ContractFixture -Root (Join-Path $temporaryRoot 'tests-library')
    $testsLibraryPath = Join-Path $testsLibrary 'tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj'
    (Get-Content -LiteralPath $testsLibraryPath -Raw -Encoding UTF8).Replace('<OutputType>Exe</OutputType>', '<OutputType>Library</OutputType>') |
        Set-Content -LiteralPath $testsLibraryPath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $testsLibrary
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 Tests project must set UseWPF=true, OutputType=Exe, RuntimeFrameworkVersion=10.0.10, and RollForward=Disable.'

    $testsWithoutWpf = New-ContractFixture -Root (Join-Path $temporaryRoot 'tests-without-wpf')
    $testsWithoutWpfPath = Join-Path $testsWithoutWpf 'tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj'
    (Get-Content -LiteralPath $testsWithoutWpfPath -Raw -Encoding UTF8).Replace('<UseWPF>true</UseWPF>', '<UseWPF>false</UseWPF>') |
        Set-Content -LiteralPath $testsWithoutWpfPath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $testsWithoutWpf
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 Tests project must set UseWPF=true, OutputType=Exe, RuntimeFrameworkVersion=10.0.10, and RollForward=Disable.'

    foreach ($runtimeMutation in @(
        @{ Name = 'runtime-version'; Old = '<RuntimeFrameworkVersion>10.0.10</RuntimeFrameworkVersion>'; New = '<RuntimeFrameworkVersion>10.0.11</RuntimeFrameworkVersion>' },
        @{ Name = 'roll-forward'; Old = '<RollForward>Disable</RollForward>'; New = '<RollForward>LatestPatch</RollForward>' }
    )) {
        $runtimeFixture = New-ContractFixture -Root (Join-Path $temporaryRoot ("tests-" + $runtimeMutation.Name))
        $runtimeProject = Join-Path $runtimeFixture 'tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj'
        (Get-Content -LiteralPath $runtimeProject -Raw -Encoding UTF8).Replace(
            $runtimeMutation.Old,
            $runtimeMutation.New
        ) | Set-Content -LiteralPath $runtimeProject -Encoding UTF8
        Assert-ThrowsExact -Action {
            Get-P0R1PrototypeState -PrototypeRoot $runtimeFixture
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 Tests project must set UseWPF=true, OutputType=Exe, RuntimeFrameworkVersion=10.0.10, and RollForward=Disable.'
    }

    # Given: A Core project using an undeclared SDK or architecture override.
    # When/Then: Every project-level toolchain identity is fixed before restore.
    foreach ($coreMutation in @('Sdk', 'Platform')) {
        $coreFixture = New-ContractFixture -Root (Join-Path $temporaryRoot ("core-$coreMutation"))
        $corePath = Join-Path $coreFixture 'src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj'
        $coreSource = Get-Content -LiteralPath $corePath -Raw -Encoding UTF8
        if ($coreMutation -eq 'Sdk') {
            $coreSource = $coreSource.Replace('Sdk="Microsoft.NET.Sdk"', 'Sdk="Forbidden.External.Sdk"')
        }
        else {
            $coreSource = $coreSource.Replace(
                '<TargetFramework>net10.0-windows</TargetFramework>',
                '<TargetFramework>net10.0-windows</TargetFramework><PlatformTarget>arm64</PlatformTarget>'
            )
        }
        Set-Content -LiteralPath $corePath -Value $coreSource -Encoding UTF8
        Assert-ThrowsExact -Action {
            Get-P0R1PrototypeState -PrototypeRoot $coreFixture
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 Core project must use Microsoft.NET.Sdk, net10.0-windows, and PlatformTarget=x64.'
    }

    # Given: Every explicit dependency-capable MSBuild element forbidden by the review contract.
    # When/Then: Each element is rejected by name and repository-relative file.
    $forbiddenElements = @(
        @{ Name = 'Reference'; Fragment = '<ItemGroup><Reference Include="System.Xml" /></ItemGroup>' },
        @{ Name = 'COMReference'; Fragment = '<ItemGroup><COMReference Include="Legacy" /></ItemGroup>' },
        @{ Name = 'NativeReference'; Fragment = '<ItemGroup><NativeReference Include="native.manifest" /></ItemGroup>' },
        @{ Name = 'Analyzer'; Fragment = '<ItemGroup><Analyzer Include="analyzer.dll" /></ItemGroup>' },
        @{ Name = 'PackageReference'; Fragment = '<ItemGroup><PackageReference Include="Forbidden.Package" Version="1.0.0" /></ItemGroup>' },
        @{ Name = 'PackageDownload'; Fragment = '<ItemGroup><PackageDownload Include="Forbidden.Package" Version="[1.0.0]" /></ItemGroup>' },
        @{ Name = 'PackageVersion'; Fragment = '<ItemGroup><PackageVersion Include="Forbidden.Package" Version="1.0.0" /></ItemGroup>' },
        @{ Name = 'UsingTask'; Fragment = '<UsingTask TaskName="Forbidden.Task" AssemblyFile="task.dll" />' },
        @{ Name = 'Import'; Fragment = '<Import Project="forbidden.targets" />' }
    )
    foreach ($case in $forbiddenElements) {
        $fixture = New-ContractFixture -Root (Join-Path $temporaryRoot ("forbidden-" + $case.Name.ToLowerInvariant()))
        $coreProject = Join-Path $fixture 'src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj'
        Add-FixtureXml -ProjectPath $coreProject -Fragment $case.Fragment
        Assert-ThrowsExact -Action {
            Assert-P0R1RestoreIsolation -PrototypeRoot $fixture
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "Forbidden MSBuild element '$($case.Name)' in 'src/AnalogBoard.ScatterRendering.Core/AnalogBoard.ScatterRendering.Core.csproj'."
    }

    # Given: Every restore source/fallback/config override.
    # When/Then: Each property is rejected exactly before dotnet runs.
    $restoreProperties = @(
        'RestoreSources',
        'RestoreAdditionalProjectSources',
        'RestoreFallbackFolders',
        'RestoreAdditionalProjectFallbackFolders',
        'RestoreConfigFile',
        'RestorePackagesPath'
    )
    foreach ($propertyName in $restoreProperties) {
        $fixture = New-ContractFixture -Root (Join-Path $temporaryRoot ("restore-" + $propertyName.ToLowerInvariant()))
        $coreProject = Join-Path $fixture 'src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj'
        Add-FixtureXml -ProjectPath $coreProject -Fragment "<PropertyGroup><$propertyName>forbidden</$propertyName></PropertyGroup>"
        Assert-ThrowsExact -Action {
            Assert-P0R1RestoreIsolation -PrototypeRoot $fixture
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "Restore override '$propertyName' is forbidden in 'src/AnalogBoard.ScatterRendering.Core/AnalogBoard.ScatterRendering.Core.csproj'."
    }

    # Given: A ProjectReference that escapes the prototype root.
    # When/Then: Resolution outside the root is rejected before file access.
    $outsideReference = New-ContractFixture -Root (Join-Path $temporaryRoot 'outside-reference')
    $outsideTests = Join-Path $outsideReference 'tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj'
    (Get-Content -LiteralPath $outsideTests -Raw -Encoding UTF8).Replace(
        '..\..\src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj',
        '..\..\..\outside.csproj'
    ) | Set-Content -LiteralPath $outsideTests -Encoding UTF8
    Assert-ThrowsExact -Action {
        Assert-P0R1RestoreIsolation -PrototypeRoot $outsideReference
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "ProjectReference from 'tests/AnalogBoard.ScatterRendering.Tests/AnalogBoard.ScatterRendering.Tests.csproj' with Include '..\..\..\outside.csproj' resolves outside the prototype root."

    # Given: A within-root ProjectReference not present in the three-edge allowlist.
    # When/Then: The unexpected edge is rejected exactly.
    $wrongReference = New-ContractFixture -Root (Join-Path $temporaryRoot 'wrong-reference')
    $wrongCore = Join-Path $wrongReference 'src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj'
    Add-FixtureXml -ProjectPath $wrongCore -Fragment '<ItemGroup><ProjectReference Include="..\..\tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj" /></ItemGroup>'
    Assert-ThrowsExact -Action {
        Assert-P0R1RestoreIsolation -PrototypeRoot $wrongReference
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "ProjectReference from 'src/AnalogBoard.ScatterRendering.Core/AnalogBoard.ScatterRendering.Core.csproj' to 'tests/AnalogBoard.ScatterRendering.Tests/AnalogBoard.ScatterRendering.Tests.csproj' is not allowed."

    # Given: A stale generated wildcard target beneath one exact project root.
    # When: Generated build roots are cleared before restore/build.
    # Then: The unpinned target and its obj root are gone without touching source.
    $generatedFixture = New-ContractFixture -Root (Join-Path $temporaryRoot 'generated-input')
    $generatedObj = Join-Path $generatedFixture 'src\AnalogBoard.ScatterRendering.Core\obj'
    $null = New-Item -ItemType Directory -Path $generatedObj -Force
    '<Project><PropertyGroup><Optimize>false</Optimize></PropertyGroup></Project>' |
        Set-Content -LiteralPath (Join-Path $generatedObj 'AnalogBoard.ScatterRendering.Core.csproj.evil.targets') -Encoding UTF8
    $clearResult = Clear-P0R1GeneratedBuildRoots -PrototypeRoot $generatedFixture
    Assert-Equal -Actual $clearResult.RemovedRootCount -Expected 1 -Message 'Generated root removal count'
    Assert-Equal -Actual (Test-Path -LiteralPath $generatedObj) -Expected $false -Message 'Generated obj root removal'
    Assert-Equal -Actual (Test-Path -LiteralPath (Join-Path $generatedFixture 'src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj')) -Expected $true -Message 'Generated cleanup source preservation'

    # Given: One valid bounded final test summary.
    # When: It is parsed.
    # Then: Exact total/passed/failed counts are returned.
    $summary = Get-P0R1TestSummary -OutputLines @('PASS first', 'SUMMARY total=2 passed=2 failed=0')
    Assert-Equal -Actual $summary.Total -Expected 2 -Message 'Summary total'
    Assert-Equal -Actual $summary.Passed -Expected 2 -Message 'Summary passed'
    Assert-Equal -Actual $summary.Failed -Expected 0 -Message 'Summary failed'

    # Given: Missing, duplicate, zero, nonzero, and inconsistent summaries.
    # When/Then: Every malformed outcome is rejected with an exact message.
    $summaryFailures = @(
        @{ Lines = @('PASS only'); Expected = 'Test runner output must contain exactly one SUMMARY line; found 0.' },
        @{ Lines = @('SUMMARY total=1 passed=1 failed=0', 'SUMMARY total=1 passed=1 failed=0'); Expected = 'Test runner output must contain exactly one SUMMARY line; found 2.' },
        @{ Lines = @('SUMMARY total=0 passed=0 failed=0'); Expected = 'Test runner summary must report total>0; got total=0 passed=0 failed=0.' },
        @{ Lines = @('SUMMARY total=3 passed=2 failed=1'); Expected = 'Test runner summary must report failed=0; got total=3 passed=2 failed=1.' },
        @{ Lines = @('SUMMARY total=3 passed=1 failed=0'); Expected = 'Test runner summary is inconsistent: total=3, passed=1, failed=0.' },
        @{ Lines = @('SUMMARY total=1 passed=1 failed=0', 'unexpected trailing output'); Expected = 'Test runner SUMMARY line must be the final non-empty line.' }
    )
    foreach ($case in $summaryFailures) {
        Assert-ThrowsExact -Action {
            Get-P0R1TestSummary -OutputLines $case.Lines
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage $case.Expected
    }

    # Given: One bounded development-only observation emitted before the final summary.
    # When: Its JSON contract is parsed.
    # Then: Scope, hard fixture, pending-one, hash, and non-official status are retained.
    $observationJson = '{"schema_id":"analogboard.scatter-rendering.development-observation.v1","development_only":true,"official_acceptance":false,"fixture_id":"AB-P0-R1-HARD-SCATTER-v1","event_count":100001,"width":512,"height":512,"iterations":10,"frame_ms_p95":12.5,"frame_ms_max":14.0,"allocated_bytes_per_frame":128,"core_scheduler_test_double_submit_p99_ms":0.02,"poster_identity":"single_slot_test_double","pending_work_max":1,"logical_pending_work_max":1,"poster_accepted_callbacks":1000,"poster_completed_callbacks":1000,"poster_aborted_callbacks":0,"coalesced_frames":0,"raster_sha256":"255bf3f549baa92d87a65111c37bed815f0b74c3452a387c6a1cc6d168b61780","machine":"fixture"}'
    $observation = Get-P0R1DevelopmentObservation -OutputLines @(
        "OBSERVATION $observationJson",
        'SUMMARY total=1 passed=1 failed=0'
    )
    Assert-Equal -Actual $observation -Expected $observationJson -Message 'Development observation JSON'

    foreach ($invalidObservation in @(
        @{ Json = $observationJson.Replace('"official_acceptance":false', '"official_acceptance":true,"official_acceptance":false'); Expected = 'P0-R1 development observation must not contain duplicate JSON property names: official_acceptance.' },
        @{ Json = $observationJson.Replace('"machine":"fixture"', '"unexpected":0,"machine":"fixture"'); Expected = 'P0-R1 development observation must contain only the exact versioned field set.' },
        @{ Json = $observationJson.Replace('"development_only":true', '"development_only":false'); Expected = 'P0-R1 observation must be development-only and must not claim official acceptance.' },
        @{ Json = $observationJson.Replace('"development_only":true', '"development_only":"true"'); Expected = 'P0-R1 development observation field must be a JSON boolean: development_only.' },
        @{ Json = $observationJson.Replace('"official_acceptance":false', '"official_acceptance":"false"'); Expected = 'P0-R1 development observation field must be a JSON boolean: official_acceptance.' },
        @{ Json = $observationJson.Replace('"event_count":100001', '"event_count":100000'); Expected = 'P0-R1 observation must use the 100001-event 512x512 hard scatter fixture.' },
        @{ Json = $observationJson.Replace('"pending_work_max":1', '"pending_work_max":2'); Expected = 'P0-R1 observation pending_work_max must be between 0 and 1.' },
        @{ Json = $observationJson.Replace('"logical_pending_work_max":1', '"logical_pending_work_max":0'); Expected = 'P0-R1 observation physical and logical pending maxima must both equal one.' },
        @{ Json = $observationJson.Replace('"event_count":100001', '"event_count":100000.6'); Expected = 'P0-R1 development observation field must be an integer JSON number: event_count.' },
        @{ Json = $observationJson.Replace('"frame_ms_p95":12.5', '"frame_ms_p95":15.0'); Expected = 'P0-R1 observation frame_ms_max must be greater than or equal to frame_ms_p95.' },
        @{ Json = $observationJson.Replace('"frame_ms_p95":12.5', '"frame_ms_p95":"12.5"'); Expected = 'P0-R1 development observation field must be a JSON number: frame_ms_p95.' },
        @{ Json = $observationJson.Replace('"frame_ms_max":14.0', '"frame_ms_max":"14.0"'); Expected = 'P0-R1 development observation field must be a JSON number: frame_ms_max.' },
        @{ Json = $observationJson.Replace('"core_scheduler_test_double_submit_p99_ms":0.02', '"core_scheduler_test_double_submit_p99_ms":"0.02"'); Expected = 'P0-R1 development observation field must be a JSON number: core_scheduler_test_double_submit_p99_ms.' },
        @{ Json = $observationJson.Replace('"coalesced_frames":0', '"coalesced_frames":0.5'); Expected = 'P0-R1 development observation field must be an integer JSON number: coalesced_frames.' },
        @{ Json = $observationJson.Replace('"poster_completed_callbacks":1000', '"poster_completed_callbacks":999'); Expected = 'P0-R1 observation poster accepted callbacks must equal completed plus aborted callbacks.' },
        @{ Json = $observationJson.Replace('"coalesced_frames":0', '"coalesced_frames":1'); Expected = 'P0-R1 observation fixed scheduler smoke counts mismatch.' },
        @{ Json = $observationJson.Replace('"fixture_id":"AB-P0-R1-HARD-SCATTER-v1"', '"fixture_id":"drift"'); Expected = 'P0-R1 observation fixture identity mismatch.' },
        @{ Json = $observationJson.Replace('"raster_sha256":"255bf3f549baa92d87a65111c37bed815f0b74c3452a387c6a1cc6d168b61780"', '"raster_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"'); Expected = 'P0-R1 observation raster_sha256 does not match the hard fixture.' },
        @{ Json = $observationJson.Replace('"machine":"fixture"', '"machine":123'); Expected = 'P0-R1 development observation field must be a JSON string: machine.' },
        @{ Json = $observationJson.Replace('"machine":"fixture"', '"machine":" "'); Expected = 'P0-R1 observation machine identity must not be empty.' }
    )) {
        Assert-ThrowsExact -Action {
            Get-P0R1DevelopmentObservation -OutputLines @("OBSERVATION $($invalidObservation.Json)")
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage $invalidObservation.Expected
    }
    Assert-ThrowsExact -Action {
        Get-P0R1DevelopmentObservation -OutputLines @(
            "OBSERVATION $($observationJson.Replace(',"coalesced_frames":0', ''))"
        )
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 development observation field is absent: coalesced_frames.'

    # Given: Separate combined and headroom development summaries.
    # When: Their exact schemas and correlations are parsed.
    # Then: Neither can claim or substitute for official acceptance.
    $combinedJson = '{"schema_id":"analogboard.scatter-rendering.combined-development-observation.v1","observation_kind":"hard-combined-compatible-pc-smoke","development_only":true,"official_acceptance":false,"may_substitute_official":false,"fixture_id":"AB-P0-R1-HARD-COMBINED-v1","event_count":100001,"scatter_width":512,"scatter_height":512,"gmi_width":512,"gmi_height":512,"selected_gmi_channel":"fsGMI","gmi_waveform_count":100,"gmi_sample_count":2400,"warmup_iterations":3,"measurement_iterations":10,"combined_iteration_ms_p95":8.5,"combined_iteration_ms_max":9.0,"scatter_delivered_update_rate_hz":110.0,"gmi_delivered_update_rate_hz":110.0,"gmi_max_update_interval_ms":9.0,"input_latency_ms_p95":0.5,"input_latency_ms_max":0.7,"allocated_bytes_per_iteration":2424,"scatter_publish_p99_ms":0.04,"gmi_publish_p99_ms":0.01,"scatter_pending_frame_max":1,"scatter_pending_callback_max":1,"gmi_pending_frame_max":1,"gmi_pending_callback_max":1,"scatter_rendered_count":13,"gmi_rendered_count":13,"scatter_coalesced_count":0,"gmi_coalesced_count":0,"input_sentinel_count":10,"dispatcher_priority_contract":"Input_above_Background","scatter_raster_sha256":"255bf3f549baa92d87a65111c37bed815f0b74c3452a387c6a1cc6d168b61780","gmi_raster_sha256":"d02e42158d6b89d39b342a307557dcc3013f908f49e6e7f3d4f43edbb4393c88","machine":"fixture"}'
    $combined = Get-P0R1CombinedDevelopmentObservation -OutputLines @(
        "COMBINED_DEVELOPMENT_OBSERVATION $combinedJson"
    )
    Assert-Equal -Actual $combined -Expected $combinedJson -Message 'Combined development observation JSON'
    foreach ($case in @(
        @{ Json = $combinedJson.Replace('"official_acceptance":false', '"official_acceptance":true,"official_acceptance":false'); Expected = 'P0-R1 combined development observation must not contain duplicate JSON property names: official_acceptance.' },
        @{ Json = $combinedJson.Replace('"may_substitute_official":false', '"may_substitute_official":true'); Expected = 'P0-R1 combined observation must remain development-only, non-official, and non-substituting.' },
        @{ Json = $combinedJson.Replace('"gmi_pending_frame_max":1', '"gmi_pending_frame_max":2'); Expected = 'P0-R1 combined observation pending maxima must equal one per feed.' },
        @{ Json = $combinedJson.Replace('"input_sentinel_count":10', '"input_sentinel_count":9'); Expected = 'P0-R1 combined observation fixed iteration counts are inconsistent.' },
        @{ Json = $combinedJson.Replace('"gmi_delivered_update_rate_hz":110.0', '"gmi_delivered_update_rate_hz":0.0'); Expected = 'P0-R1 combined observation delivered update rates must be positive.' },
        @{ Json = $combinedJson.Replace('"combined_iteration_ms_max":9.0', '"combined_iteration_ms_max":"9.0"'); Expected = 'P0-R1 combined observation field must be a JSON number: combined_iteration_ms_max.' }
    )) {
        Assert-ThrowsExact -Action {
            Get-P0R1CombinedDevelopmentObservation -OutputLines @(
                "COMBINED_DEVELOPMENT_OBSERVATION $($case.Json)"
            )
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage $case.Expected
    }

    $headroomJson = '{"schema_id":"analogboard.scatter-rendering.headroom-development-observation.v1","observation_kind":"three-tile-headroom-compatible-pc","development_only":true,"official_acceptance":false,"may_substitute_hard_gate":false,"fixture_id":"AB-P0-R1-HEADROOM-v1","event_count":131072,"width":1024,"height":1024,"tile_count":3,"scatter_tile_count":2,"gmi_tile_count":1,"selected_gmi_channel":"fsGMI","gmi_waveform_count":100,"gmi_sample_count":2400,"warmup_iterations":1,"measurement_iterations":3,"three_tile_iteration_ms_p95":28.0,"three_tile_iteration_ms_max":29.0,"allocated_bytes_per_iteration":192,"scatter_a_raster_sha256":"0ffd755ea97e90ab1f8a186cc20e58f30378196be0d6ba3b1d068cb5b59afa45","scatter_b_raster_sha256":"9c1c255558b089f96eeb6336c55b0fbe254b05f47ea815c6e230ca05e8b31b17","gmi_raster_sha256":"417e8cf7f21ab09680e98a2aa6b7aabd0479a3457a52e55f15b6b3d41bd1156b","machine":"fixture"}'
    $headroom = Get-P0R1HeadroomDevelopmentObservation -OutputLines @(
        "HEADROOM_DEVELOPMENT_OBSERVATION $headroomJson"
    )
    Assert-Equal -Actual $headroom -Expected $headroomJson -Message 'Headroom development observation JSON'
    foreach ($case in @(
        @{ Json = $headroomJson.Replace('"official_acceptance":false', '"official_acceptance":true,"official_acceptance":false'); Expected = 'P0-R1 headroom development observation must not contain duplicate JSON property names: official_acceptance.' },
        @{ Json = $headroomJson.Replace('"may_substitute_hard_gate":false', '"may_substitute_hard_gate":true'); Expected = 'P0-R1 headroom observation must remain development-only, non-official, and non-substituting.' },
        @{ Json = $headroomJson.Replace('"tile_count":3', '"tile_count":2'); Expected = 'P0-R1 headroom observation must use 131072 events, 1024-square output, and exactly three tiles.' },
        @{ Json = $headroomJson.Replace('"three_tile_iteration_ms_max":29.0', '"three_tile_iteration_ms_max":27.0'); Expected = 'P0-R1 headroom observation maximum must be greater than or equal to p95.' }
    )) {
        Assert-ThrowsExact -Action {
            Get-P0R1HeadroomDevelopmentObservation -OutputLines @(
                "HEADROOM_DEVELOPMENT_OBSERVATION $($case.Json)"
            )
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage $case.Expected
    }

    $completeRepository = Split-Path -Parent (Split-Path -Parent $completePrototype)
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'global.json') -Destination $completeRepository
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot '.gitattributes') -Destination $completeRepository
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot '.editorconfig') -Destination $completeRepository
    foreach ($fixtureName in @(
        'display-transform-contract-v1.json',
        'density-raster-contract-v1.json',
        'gmi-raster-contract-v1.json'
    )) {
        $fixtureDestination = Join-Path `
            $completeRepository `
            "docs\reference\scatter-rendering\phase0\$fixtureName"
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $fixtureDestination) -Force
        Copy-Item `
            -LiteralPath (Join-Path $RepositoryRoot "docs\reference\scatter-rendering\phase0\$fixtureName") `
            -Destination $fixtureDestination
    }

    # Given: A complete scaffold and a successful fake dotnet boundary.
    # When: Focused verification is orchestrated.
    # Then: Commands are isolated and the bounded test summary reaches the result.
    $calls = [System.Collections.Generic.List[object]]::new()
    $successfulInvoker = {
        param([string[]]$Arguments, [string]$WorkingDirectory)
        $calls.Add([pscustomobject]@{ Arguments = @($Arguments); WorkingDirectory = $WorkingDirectory })
        $output = switch ($Arguments[0]) {
            '--version' { @('10.0.302') }
            '--list-runtimes' { @('Microsoft.WindowsDesktop.App 10.0.10 [C:\dotnet]') }
            'run' { @("OBSERVATION $observationJson", "COMBINED_DEVELOPMENT_OBSERVATION $combinedJson", "HEADROOM_DEVELOPMENT_OBSERVATION $headroomJson", 'PASS fixture', 'SUMMARY total=2 passed=2 failed=0') }
            default { @('fixture command passed') }
        }
        return [pscustomobject]@{ ExitCode = 0; Output = $output }
    }
    $result = Invoke-P0R1FocusedVerification `
        -RepositoryRoot (Split-Path -Parent (Split-Path -Parent $completePrototype)) `
        -PrototypeRoot $completePrototype `
        -Configuration 'Release' `
        -Architecture 'x64' `
        -DotNetInvoker $successfulInvoker
    Assert-Equal -Actual $result.Status -Expected 'Pass' -Message 'Focused status'
    Assert-Equal -Actual $result.TestsTotal -Expected 2 -Message 'Focused tests total'
    Assert-Equal -Actual $result.TestsPassed -Expected 2 -Message 'Focused tests passed'
    Assert-Equal -Actual $result.TestsFailed -Expected 0 -Message 'Focused tests failed'
    Assert-Equal -Actual $result.DevelopmentObservation -Expected $observationJson -Message 'Focused development observation'
    Assert-Equal -Actual $result.CombinedDevelopmentObservation -Expected $combinedJson -Message 'Focused combined observation'
    Assert-Equal -Actual $result.HeadroomDevelopmentObservation -Expected $headroomJson -Message 'Focused headroom observation'
    Assert-Equal -Actual $calls.Count -Expected 5 -Message 'dotnet command count'
    Assert-Contains -Actual $calls[2].Arguments -Expected '--configfile' -Message 'Restore config isolation'
    Assert-Contains -Actual $calls[2].Arguments -Expected '--disable-build-servers' -Message 'Restore build-server isolation'
    Assert-Contains -Actual $calls[3].Arguments -Expected '--no-restore' -Message 'Build restore isolation'
    Assert-Contains -Actual $calls[3].Arguments -Expected '--disable-build-servers' -Message 'Build server isolation'
    Assert-Contains `
        -Actual $calls[3].Arguments `
        -Expected "-p:P0R1MeasuredSourceTreeSha256=$((Get-P0R1MeasuredSourceTreeHash -RepositoryRoot $completeRepository).Sha256)" `
        -Message 'Build measured-source identity'
    $gitExecutablePath = (Resolve-Path -LiteralPath (Get-Command git.exe -CommandType Application).Source).Path
    Assert-Contains `
        -Actual $calls[3].Arguments `
        -Expected "-p:P0R1GitExecutablePath=$gitExecutablePath" `
        -Message 'Build Git executable identity'
    Assert-Contains `
        -Actual $calls[3].Arguments `
        -Expected "-p:P0R1GitExecutableSha256=$((Get-FileHash -LiteralPath $gitExecutablePath -Algorithm SHA256).Hash.ToLowerInvariant())" `
        -Message 'Build Git executable hash'
    Assert-Contains -Actual $calls[4].Arguments -Expected '--no-build' -Message 'Runner build isolation'
    Assert-Contains -Actual $calls[4].Arguments -Expected '--no-restore' -Message 'Runner restore isolation'
    Assert-Contains -Actual $calls[4].Arguments -Expected '--disable-build-servers' -Message 'Runner build-server isolation'

    # Given: A dynamic subprocess failure during restore.
    # When/Then: Only the dedicated dynamic-output helper uses pattern matching.
    $failingInvoker = {
        param([string[]]$Arguments, [string]$WorkingDirectory)
        if ($Arguments[0] -eq '--version') {
            return [pscustomobject]@{ ExitCode = 0; Output = @('10.0.302') }
        }
        if ($Arguments[0] -eq '--list-runtimes') {
            return [pscustomobject]@{ ExitCode = 0; Output = @('Microsoft.WindowsDesktop.App 10.0.10 [C:\dotnet]') }
        }
        return [pscustomobject]@{ ExitCode = 17; Output = @('simulated restore failure') }
    }
    Assert-ThrowsDynamicOutput -Action {
        Invoke-P0R1FocusedVerification `
            -RepositoryRoot (Split-Path -Parent (Split-Path -Parent $completePrototype)) `
            -PrototypeRoot $completePrototype `
            -Configuration 'Release' `
            -Architecture 'x64' `
            -DotNetInvoker $failingInvoker
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessagePattern '*dotnet restore*exit code 17*simulated restore failure*'
}
finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}

# Given: The real Batch 1 scaffold after all repository-local files exist.
# When: Its exact dependency, graph, solution, WPF, and test-runner contracts are checked.
# Then: It is accepted only after Debug mappings are removed.
$realPrototype = Join-Path $RepositoryRoot 'prototypes\scatter-rendering'
$realProject = Join-Path $realPrototype 'tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj'
$ambientProbeNames = @('Optimize', 'DefineConstants', 'DOTNET_STARTUP_HOOKS', 'COMPlus_TieredCompilation')
$priorAmbientProbeValues = @{}
try {
    foreach ($variableName in $ambientProbeNames) {
        $priorAmbientProbeValues[$variableName] = [Environment]::GetEnvironmentVariable($variableName, 'Process')
    }
    [Environment]::SetEnvironmentVariable('Optimize', 'false', 'Process')
    [Environment]::SetEnvironmentVariable('DefineConstants', 'AMBIENT_DEFINE', 'Process')
    [Environment]::SetEnvironmentVariable('DOTNET_STARTUP_HOOKS', 'C:\outside\hook.dll', 'Process')
    [Environment]::SetEnvironmentVariable('COMPlus_TieredCompilation', '0', 'Process')
    $sanitizedProbe = Invoke-P0R1SanitizedDotNet `
        -Arguments @('msbuild', $realProject, '-nologo', '-getProperty:Optimize', '-p:Configuration=Release', '-p:Platform=x64') `
        -WorkingDirectory $realPrototype
    Assert-Equal -Actual $sanitizedProbe.ExitCode -Expected 0 -Message 'Sanitized child process exit code'
    $sanitizedProbeLines = @($sanitizedProbe.Output | Where-Object { $_.Trim() -ne '' })
    Assert-Equal -Actual $sanitizedProbeLines[$sanitizedProbeLines.Count - 1].Trim() -Expected 'true' -Message 'Sanitized child ignores ambient Optimize'
    $sharedCompilationProbe = Invoke-P0R1SanitizedDotNet `
        -Arguments @('msbuild', $realProject, '-nologo', '-getProperty:UseSharedCompilation', '-p:Configuration=Release', '-p:Platform=x64') `
        -WorkingDirectory $realPrototype
    Assert-Equal -Actual $sharedCompilationProbe.ExitCode -Expected 0 -Message 'Shared compilation probe exit code'
    $sharedCompilationLines = @($sharedCompilationProbe.Output | Where-Object { $_.Trim() -ne '' })
    Assert-Equal -Actual $sharedCompilationLines[$sharedCompilationLines.Count - 1].Trim() -Expected 'false' -Message 'Shared compiler server disabled'

    $officialGitExecutablePath = (
        Resolve-Path `
            -LiteralPath (Get-Command git.exe -CommandType Application).Source `
            -ErrorAction Stop
    ).Path
    # Given: The existing fully qualified Git executable used by Official mode.
    # When: A real sanitized Official-preflight child is launched.
    $officialSanitizedProbe = Invoke-P0R1SanitizedDotNet `
        -Arguments @('--version') `
        -WorkingDirectory $realPrototype `
        -GitExecutablePath $officialGitExecutablePath `
        -OfficialPreflight
    # Then: Windows PowerShell 5.1 accepts the path and the child succeeds.
    Assert-Equal `
        -Actual $officialSanitizedProbe.ExitCode `
        -Expected 0 `
        -Message 'Official sanitized child process exit code'
    Assert-Contains `
        -Actual $officialSanitizedProbe.Output `
        -Expected '10.0.302' `
        -Message 'Official sanitized child output'

    $officialPathError =
        'Official performance execution requires one absolute Git executable path.'
    # Given: Official preflight omits the Git executable path.
    # When/Then: The missing path fails with the exact typed contract.
    Assert-ThrowsExact -Action {
        Invoke-P0R1SanitizedDotNet `
            -Arguments @('--version') `
            -WorkingDirectory $realPrototype `
            -OfficialPreflight
    } -ExpectedType 'System.InvalidOperationException' `
        -ExpectedMessage $officialPathError

    $missingGitExecutablePath = Join-Path `
        ([IO.Path]::GetTempPath()) `
        ("p0r1-missing-git-" + [guid]::NewGuid().ToString('N') + '.exe')
    foreach ($invalidGitExecutablePath in @(
        '',
        'git.exe',
        'C:git.exe',
        $missingGitExecutablePath
    )) {
        # Given: An empty, relative, drive-relative, or missing Git path.
        # When/Then: Official preflight rejects it with the same exact contract.
        Assert-ThrowsExact -Action {
            Invoke-P0R1SanitizedDotNet `
                -Arguments @('--version') `
                -WorkingDirectory $realPrototype `
                -GitExecutablePath $invalidGitExecutablePath `
                -OfficialPreflight
        } -ExpectedType 'System.InvalidOperationException' `
            -ExpectedMessage $officialPathError
    }
}
finally {
    foreach ($variableName in $ambientProbeNames) {
        [Environment]::SetEnvironmentVariable($variableName, $priorAmbientProbeValues[$variableName], 'Process')
    }
}
$null = Assert-P0R1RestoreIsolation -PrototypeRoot $realPrototype
$realState = Get-P0R1PrototypeState -PrototypeRoot $realPrototype
Assert-Equal -Actual $realState.ProjectCount -Expected 3 -Message 'Real scaffold project count'
$repositoryAttributesSource = Get-Content `
    -LiteralPath (Join-Path $RepositoryRoot '.gitattributes') `
    -Raw `
    -Encoding UTF8
foreach ($requiredMeasuredSourceAttribute in @(
    '/.gitattributes text eol=lf',
    '/.editorconfig text eol=lf',
    '/global.json text eol=lf',
    '/prototypes/scatter-rendering/** text eol=lf',
    '/docs/reference/scatter-rendering/phase0/display-transform-contract-v1.json text eol=lf',
    '/docs/reference/scatter-rendering/phase0/density-raster-contract-v1.json text eol=lf',
    '/docs/reference/scatter-rendering/phase0/gmi-raster-contract-v1.json text eol=lf',
    '/docs/reference/scatter-rendering/phase0/performance-reference-profile-v1.json text eol=lf',
    '/Directory.Build.props text eol=lf',
    '/Directory.Build.targets text eol=lf',
    '/Directory.Packages.props text eol=lf',
    '/prototypes/Directory.Build.props text eol=lf',
    '/prototypes/Directory.Build.targets text eol=lf',
    '/prototypes/Directory.Packages.props text eol=lf'
)) {
    if (-not $repositoryAttributesSource.Contains($requiredMeasuredSourceAttribute)) {
        throw "Measured source EOL authority is absent: $requiredMeasuredSourceAttribute"
    }
}
$repositoryEditorConfigSource = Get-Content `
    -LiteralPath (Join-Path $RepositoryRoot '.editorconfig') `
    -Raw `
    -Encoding UTF8
foreach ($requiredMeasuredSourceEditorConfig in @(
    '[.editorconfig]',
    '[.gitattributes]',
    '[global.json]',
    '[prototypes/scatter-rendering/**]',
    '[docs/reference/scatter-rendering/phase0/*.json]',
    '[Directory.Build.props]',
    '[Directory.Build.targets]',
    '[Directory.Packages.props]',
    '[prototypes/Directory.Build.props]',
    '[prototypes/Directory.Build.targets]',
    '[prototypes/Directory.Packages.props]'
)) {
    $sectionPattern = '(?ms)^' +
        [regex]::Escape($requiredMeasuredSourceEditorConfig) +
        '\r?\nend_of_line = lf(?:\r?\n|$)'
    if ($repositoryEditorConfigSource -notmatch $sectionPattern) {
        throw "Measured source EditorConfig LF authority is absent: $requiredMeasuredSourceEditorConfig"
    }
}
$buildIdentityTargetsPath = Join-Path $realPrototype 'Directory.Build.targets'
$buildIdentityTargetsSource = Get-Content `
    -LiteralPath $buildIdentityTargetsPath `
    -Raw `
    -Encoding UTF8
foreach ($requiredBuildIdentityBoundary in @(
    'ValidateP0R1BuildIdentity',
    '<AssemblyMetadata Include="P0R1MeasuredSourceTreeSha256"',
    '<AssemblyMetadata Include="P0R1GitExecutablePath"',
    '<AssemblyMetadata Include="P0R1GitExecutableSha256"',
    '<AssemblyMetadata Include="P0R1Configuration"',
    '<AssemblyMetadata Include="P0R1TargetFramework"',
    '<AssemblyMetadata Include="P0R1Platform"',
    '<AssemblyMetadata Include="P0R1PlatformTarget"',
    '<AssemblyMetadata Include="P0R1SdkVersion"'
)) {
    if (-not $buildIdentityTargetsSource.Contains($requiredBuildIdentityBoundary)) {
        throw "Focused build identity boundary is absent: $requiredBuildIdentityBoundary"
    }
}
$missingIdentityBuildOutput = @(
    & dotnet msbuild `
        $realProject `
        -nologo `
        -target:ValidateP0R1BuildIdentity `
        -p:Configuration=Release `
        -p:Platform=x64 2>&1 |
        ForEach-Object { $_.ToString() }
)
if ($LASTEXITCODE -eq 0 -or
    -not ($missingIdentityBuildOutput -match
        'P0R1MeasuredSourceTreeSha256 must be a lowercase SHA-256 identity\.')) {
    throw 'A direct build without focused source identity must fail closed.'
}
$debugIdentityBuildOutput = @(
    & dotnet msbuild `
        $realProject `
        -nologo `
        -target:ValidateP0R1BuildIdentity `
        -p:Configuration=Debug `
        -p:Platform=x64 `
        "-p:P0R1MeasuredSourceTreeSha256=$('a' * 64)" `
        '-p:P0R1GitExecutablePath=C:\fixture\git.exe' `
        "-p:P0R1GitExecutableSha256=$('b' * 64)" 2>&1 |
        ForEach-Object { $_.ToString() }
)
if ($LASTEXITCODE -eq 0 -or
    -not ($debugIdentityBuildOutput -match
        'P0-R1 build identity requires Configuration=Release\.')) {
    throw 'A Debug build identity must fail closed.'
}

$wrapperSource = Get-Content -LiteralPath $WrapperPath -Raw -Encoding UTF8
foreach ($requiredToken in @(
    "[ValidateSet('Focused')]",
    "[ValidateSet('Release')]",
    "[ValidateSet('x64')]",
    "`$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = '1'",
    'tests_total=',
    'tests_passed=',
    'tests_failed=',
    'development_observation=',
    'combined_development_observation=',
    'headroom_development_observation=',
    'renderer_decision='
)) {
    if ($wrapperSource -notmatch [regex]::Escape($requiredToken)) {
        throw "verify.ps1 is missing required contract token: $requiredToken"
    }
}
$environmentIndex = $wrapperSource.IndexOf("`$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = '1'", [StringComparison]::Ordinal)
$dotnetIndex = $wrapperSource.IndexOf('Invoke-P0R1FocusedVerification', [StringComparison]::Ordinal)
if ($environmentIndex -lt 0 -or $environmentIndex -gt $dotnetIndex) {
    throw 'The workload-update notification guard must be set before every dotnet invocation.'
}
$sourceEvidenceIndex = $wrapperSource.IndexOf('Assert-P0R1RendererDecisionContract', [StringComparison]::Ordinal)
if ($sourceEvidenceIndex -lt 0 -or $sourceEvidenceIndex -gt $dotnetIndex) {
    throw 'Renderer/source evidence must be validated before restore, build, or test execution.'
}
$rendererDecision = Assert-P0R1RendererDecisionContract -RepositoryRoot $RepositoryRoot
Assert-Equal -Actual $rendererDecision.DecisionId -Expected 'P0-R1-RENDERER-v1' -Message 'Renderer decision identity'
Assert-Equal -Actual $rendererDecision.SelectedCandidateId -Expected 'wpf-writeablebitmap-preallocated' -Message 'Renderer selection'
Assert-Equal -Actual $rendererDecision.Status -Expected 'accepted_at_phase_checkpoint' -Message 'Renderer decision status'
Assert-Equal -Actual $rendererDecision.OfficialAcceptance -Expected $true -Message 'Renderer Official acceptance'
Assert-Equal -Actual $rendererDecision.MeasuredSourceTreeSha256 -Expected '72ee0dc09fabe18c413e1d950b1ae11b47ddb48f0ed3545cbf97fb449a7c0a83' -Message 'Measured source tree identity'
$historicalDevelopmentSource = Get-P0R1MeasuredSourceTreeHash `
    -RepositoryRoot $RepositoryRoot `
    -ReferenceProfileState 'Absent'
Assert-Equal `
    -Actual $historicalDevelopmentSource.Sha256 `
    -Expected 'bf58f329785d69e1780c46d7d709af8679169c264d580de3b536551b2fb20964' `
    -Message 'Historical profile-absent development source identity'
Assert-Equal `
    -Actual $historicalDevelopmentSource.FileCount `
    -Expected 61 `
    -Message 'Historical profile-absent development source file count'
$expectedMeasurementCommand = 'dotnet run --project tests/AnalogBoard.ScatterRendering.Tests/AnalogBoard.ScatterRendering.Tests.csproj --configuration Release -p:Platform=x64 --no-build --no-restore --disable-build-servers --'
foreach ($relativeEvidencePath in @(
    'docs/reference/scatter-rendering/phase0/batch4-combined-development-observation.json',
    'docs/reference/scatter-rendering/phase0/batch4-headroom-development-observation.json'
)) {
    $evidence = Get-Content -LiteralPath (Join-Path $RepositoryRoot $relativeEvidencePath) -Raw -Encoding UTF8 |
        ConvertFrom-Json
    Assert-Equal -Actual $evidence.measurement_command -Expected $expectedMeasurementCommand -Message "Isolated measurement command: $relativeEvidencePath"
}

$duplicateRendererRoot = Join-Path ([IO.Path]::GetTempPath()) ("p0r1-renderer-duplicate-" + [guid]::NewGuid().ToString('N'))
try {
    foreach ($relativePath in @(
        'docs/dependencies/analogboard-p0-r1-dependencies.json',
        'docs/reference/scatter-rendering/phase0/display-transform-contract-v1.json',
        'docs/reference/scatter-rendering/phase0/density-raster-contract-v1.json',
        'docs/reference/scatter-rendering/phase0/gmi-raster-contract-v1.json',
        'docs/reference/scatter-rendering/phase0/performance-reference-profile-v1.json',
        'docs/reference/scatter-rendering/phase0/official-performance-evidence-v1.json',
        'docs/reference/scatter-rendering/phase0/batch4-combined-development-observation.json',
        'docs/reference/scatter-rendering/phase0/batch4-headroom-development-observation.json',
        'docs/reference/scatter-rendering/phase0/renderer-decision-v1.json'
    )) {
        $destination = Join-Path $duplicateRendererRoot $relativePath
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
        Copy-Item -LiteralPath (Join-Path $RepositoryRoot $relativePath) -Destination $destination
    }
    $duplicateDecisionPath = Join-Path $duplicateRendererRoot 'docs/reference/scatter-rendering/phase0/renderer-decision-v1.json'
    $duplicateDecision = (Get-Content -LiteralPath $duplicateDecisionPath -Raw -Encoding UTF8).Replace(
        '"official_acceptance": true,',
        '"official_acceptance": true, "official_acceptance": false,'
    )
    Set-Content -LiteralPath $duplicateDecisionPath -Value $duplicateDecision -Encoding UTF8
    Assert-ThrowsExact -Action {
        Assert-P0R1RendererDecisionContract -RepositoryRoot $duplicateRendererRoot
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 renderer decision must not contain duplicate JSON property names: official_acceptance.'
}
finally {
    Remove-Item -LiteralPath $duplicateRendererRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$sourceDriftOuter = Join-Path ([IO.Path]::GetTempPath()) ("p0r1-source-drift-" + [guid]::NewGuid().ToString('N'))
$sourceDriftRoot = Join-Path $sourceDriftOuter 'repo'
try {
    foreach ($relativePath in @(
        'docs/dependencies/analogboard-p0-r1-dependencies.json',
        'docs/reference/scatter-rendering/phase0/display-transform-contract-v1.json',
        'docs/reference/scatter-rendering/phase0/density-raster-contract-v1.json',
        'docs/reference/scatter-rendering/phase0/gmi-raster-contract-v1.json',
        'docs/reference/scatter-rendering/phase0/performance-reference-profile-v1.json',
        'docs/reference/scatter-rendering/phase0/official-performance-evidence-v1.json',
        'docs/reference/scatter-rendering/phase0/batch4-combined-development-observation.json',
        'docs/reference/scatter-rendering/phase0/batch4-headroom-development-observation.json',
        'docs/reference/scatter-rendering/phase0/renderer-decision-v1.json'
    )) {
        $destination = Join-Path $sourceDriftRoot $relativePath
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
        Copy-Item -LiteralPath (Join-Path $RepositoryRoot $relativePath) -Destination $destination
    }
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'global.json') -Destination $sourceDriftRoot
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot '.gitattributes') -Destination $sourceDriftRoot
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot '.editorconfig') -Destination $sourceDriftRoot
    $sourceDestination = Join-Path $sourceDriftRoot 'prototypes/scatter-rendering'
    $null = New-Item -ItemType Directory -Path $sourceDestination -Force
    foreach ($rootFile in Get-ChildItem -LiteralPath (Join-Path $RepositoryRoot 'prototypes/scatter-rendering') -File -Force) {
        Copy-Item -LiteralPath $rootFile.FullName -Destination $sourceDestination
    }
    Copy-P0R1FixtureSourceTree `
        -SourceRoot (Join-Path $RepositoryRoot 'prototypes/scatter-rendering/src') `
        -DestinationRoot (Join-Path $sourceDestination 'src')
    Copy-P0R1FixtureSourceTree `
        -SourceRoot (Join-Path $RepositoryRoot 'prototypes/scatter-rendering/tests') `
        -DestinationRoot (Join-Path $sourceDestination 'tests')

    $null = Assert-P0R1RepositoryDependencyContract -RepositoryRoot $sourceDriftRoot
    $sourcePropsPath = Join-Path $sourceDestination 'Directory.Build.props'
    (Get-Content -LiteralPath $sourcePropsPath -Raw -Encoding UTF8).Replace(
        '<DiscoverEditorConfigFiles>false</DiscoverEditorConfigFiles>',
        '<DiscoverEditorConfigFiles>true</DiscoverEditorConfigFiles>'
    ) | Set-Content -LiteralPath $sourcePropsPath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Assert-P0R1RepositoryDependencyContract -RepositoryRoot $sourceDriftRoot
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'Directory.Build.props must disable ancestor editor/global analyzer config discovery.'
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'prototypes/scatter-rendering/Directory.Build.props') -Destination $sourcePropsPath -Force

    (Get-Content -LiteralPath $sourcePropsPath -Raw -Encoding UTF8).Replace(
        '<UseSharedCompilation>false</UseSharedCompilation>',
        '<UseSharedCompilation>true</UseSharedCompilation>'
    ) | Set-Content -LiteralPath $sourcePropsPath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Assert-P0R1RepositoryDependencyContract -RepositoryRoot $sourceDriftRoot
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'Directory.Build.props must disable shared compiler-server reuse.'
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'prototypes/scatter-rendering/Directory.Build.props') -Destination $sourcePropsPath -Force

    foreach ($terminator in @(
        'Directory.Build.targets',
        'Directory.Packages.props',
        'Directory.Solution.props',
        'Directory.Solution.targets',
        'Directory.Build.rsp'
    )) {
        $terminatorPath = Join-Path $sourceDestination $terminator
        Add-Content -LiteralPath $terminatorPath -Value 'drift'
        Assert-ThrowsExact -Action {
            Assert-P0R1RepositoryDependencyContract -RepositoryRoot $sourceDriftRoot
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "P0-R1 build ancestor terminator hash mismatch: prototypes/scatter-rendering/$terminator."
        Copy-Item -LiteralPath (Join-Path $RepositoryRoot "prototypes/scatter-rendering/$terminator") -Destination $terminatorPath -Force
    }

    '<Project><PropertyGroup><Optimize>false</Optimize></PropertyGroup></Project>' |
        Set-Content -LiteralPath (Join-Path $sourceDriftOuter 'Directory.Build.targets') -Encoding UTF8
    '/p:Optimize=false' |
        Set-Content -LiteralPath (Join-Path $sourceDriftOuter 'Directory.Build.rsp') -Encoding UTF8
    $projectPath = Join-Path $sourceDriftRoot 'prototypes/scatter-rendering/tests/AnalogBoard.ScatterRendering.Tests/AnalogBoard.ScatterRendering.Tests.csproj'
    $propertyOutput = @(
        & dotnet msbuild $projectPath -nologo -getProperty:Optimize -p:Configuration=Release -p:Platform=x64 2>&1 |
            ForEach-Object { $_.ToString() }
    )
    if ($LASTEXITCODE -ne 0) {
        throw "Ancestor terminator probe failed: $($propertyOutput -join [Environment]::NewLine)"
    }
    $nonEmptyPropertyOutput = @($propertyOutput | Where-Object { $_.Trim() -ne '' })
    Assert-Equal -Actual $nonEmptyPropertyOutput[$nonEmptyPropertyOutput.Count - 1].Trim() -Expected 'true' -Message 'Ancestor build target/response isolation'

    $userExtensionImportProperties = @(
        'ImportUserLocationsByWildcardBeforeMicrosoftCommonProps',
        'ImportUserLocationsByWildcardAfterMicrosoftCommonProps',
        'ImportUserLocationsByWildcardBeforeMicrosoftCommonTargets',
        'ImportUserLocationsByWildcardAfterMicrosoftCommonTargets',
        'ImportUserLocationsByWildcardBeforeMicrosoftCSharpTargets',
        'ImportUserLocationsByWildcardAfterMicrosoftCSharpTargets'
    )
    foreach ($propertyName in $userExtensionImportProperties) {
        $userExtensionOutput = @(
            & dotnet msbuild $projectPath -nologo "-getProperty:$propertyName" -p:Configuration=Release -p:Platform=x64 2>&1 |
                ForEach-Object { $_.ToString() }
        )
        if ($LASTEXITCODE -ne 0) {
            throw "User MSBuild extension isolation probe failed for $propertyName`: $($userExtensionOutput -join [Environment]::NewLine)"
        }
        $nonEmptyUserExtensionOutput = @($userExtensionOutput | Where-Object { $_.Trim() -ne '' })
        Assert-Equal `
            -Actual $nonEmptyUserExtensionOutput[$nonEmptyUserExtensionOutput.Count - 1].Trim() `
            -Expected 'false' `
            -Message "User MSBuild extension import isolation: $propertyName"
    }

    Add-Content -LiteralPath (Join-Path $sourceDriftRoot 'prototypes/scatter-rendering/src/AnalogBoard.ScatterRendering.Core/AggregateFrame.cs') -Value '// drift'
    Assert-ThrowsExact -Action {
        Assert-P0R1RendererDecisionContract -RepositoryRoot $sourceDriftRoot
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 combined development evidence measured source tree hash does not match current prototype sources.'
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'prototypes/scatter-rendering/src/AnalogBoard.ScatterRendering.Core/AggregateFrame.cs') -Destination (Join-Path $sourceDriftRoot 'prototypes/scatter-rendering/src/AnalogBoard.ScatterRendering.Core/AggregateFrame.cs') -Force

    $propsPath = Join-Path $sourceDriftRoot 'prototypes/scatter-rendering/Directory.Build.props'
    (Get-Content -LiteralPath $propsPath -Raw -Encoding UTF8).Replace(
        '</PropertyGroup>',
        '<Optimize>false</Optimize></PropertyGroup>'
    ) | Set-Content -LiteralPath $propsPath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Assert-P0R1RendererDecisionContract -RepositoryRoot $sourceDriftRoot
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 combined development evidence measured source tree hash does not match current prototype sources.'
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'prototypes/scatter-rendering/Directory.Build.props') -Destination $propsPath -Force

    $targetsPath = Join-Path $sourceDriftRoot 'prototypes/scatter-rendering/Directory.Build.targets'
    '<Project><Target Name="Injected" BeforeTargets="CoreCompile" /></Project>' |
        Set-Content -LiteralPath $targetsPath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Assert-P0R1RendererDecisionContract -RepositoryRoot $sourceDriftRoot
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 combined development evidence measured source tree hash does not match current prototype sources.'
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'prototypes/scatter-rendering/Directory.Build.targets') -Destination $targetsPath -Force

    $xamlPath = Join-Path $sourceDriftRoot 'prototypes/scatter-rendering/src/AnalogBoard.ScatterRendering.Wpf/Injected.XAML'
    '<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" />' |
        Set-Content -LiteralPath $xamlPath -Encoding UTF8
    (Get-Item -LiteralPath $xamlPath).Attributes =
        (Get-Item -LiteralPath $xamlPath).Attributes -bor [IO.FileAttributes]::Hidden
    Assert-ThrowsExact -Action {
        Assert-P0R1RendererDecisionContract -RepositoryRoot $sourceDriftRoot
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 combined development evidence measured source tree hash does not match current prototype sources.'
    Remove-Item -LiteralPath $xamlPath -Force

    $ancestorTargetsPath = Join-Path $sourceDriftRoot 'prototypes/Directory.Build.targets'
    '<Project><PropertyGroup><Optimize>false</Optimize></PropertyGroup></Project>' |
        Set-Content -LiteralPath $ancestorTargetsPath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Assert-P0R1RendererDecisionContract -RepositoryRoot $sourceDriftRoot
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 combined development evidence measured source tree hash does not match current prototype sources.'
}
finally {
    Remove-Item -LiteralPath $sourceDriftOuter -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host 'PASS: scatter-rendering verification contract tests'

# Test execution:
# powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/scatter-rendering/tests/verify_contract_test.ps1
# Coverage target: every exported wrapper-contract branch; no external PowerShell coverage package is permitted.
