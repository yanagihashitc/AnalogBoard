[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildId,

    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,

    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,

    [Parameter(Mandatory = $true)]
    [string]$PackageRoot,

    [Parameter(Mandatory = $true)]
    [string]$ReadyRoot,

    [Parameter(Mandatory = $true)]
    [string]$PackageManifest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-RelativePath([string]$BasePath, [string]$Path) {
    $baseUri = [Uri](([IO.Path]::GetFullPath($BasePath).TrimEnd('\') + '\'))
    $pathUri = [Uri][IO.Path]::GetFullPath($Path)
    return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString())
}

function Write-ChecksumManifest(
    [string]$BasePath,
    [string[]]$RelativePaths,
    [string]$OutputPath,
    [Text.Encoding]$Encoding
) {
    $lines = foreach ($relativePath in $RelativePaths) {
        $fullPath = Join-Path $BasePath $relativePath
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            throw "Checksum input is missing: $fullPath"
        }
        '{0} *{1}' -f (Get-Sha256 $fullPath), $relativePath.Replace('\', '/')
    }
    [IO.File]::WriteAllLines($OutputPath, $lines, $Encoding)
}

$prepRoot = Join-Path $SourceRoot 'diagnostic_prep\telemetry_graceful_stop'
$manifestPath = Join-Path $OutputRoot 'manifest\build_manifest.json'
$checksumsPath = Join-Path $OutputRoot 'manifest\checksums.sha256'
$overlayAttestationPath = Join-Path $OutputRoot 'manifest\source_overlay_attestation.json'
$deltaPath = Join-Path $OutputRoot 'source_diff\graceful_stop_changes.patch'
$reviewPath = Join-Path $OutputRoot 'verification\claude_review.txt'
$unitTestLogPath = Join-Path $OutputRoot 'verification\unit_tests.txt'
$dllBuildLogPath = Join-Path $OutputRoot 'verification\release_dll_build.txt'
$appBuildLogPath = Join-Path $OutputRoot 'verification\release_app_build.txt'
$fieldIntegrityPath = Join-Path $OutputRoot 'verification\field_package_integrity.txt'
$knownBaselineDeviationsPath = Join-Path $OutputRoot 'KNOWN_BASELINE_DEVIATIONS.txt'

if (Test-Path -LiteralPath $manifestPath) {
    throw "Build manifest already exists: $manifestPath"
}
if (Test-Path -LiteralPath $ReadyRoot) {
    throw "Ready directory already exists: $ReadyRoot"
}

$package = Get-Content -Raw -LiteralPath $PackageManifest | ConvertFrom-Json
$artifactPaths = @(
    (Join-Path $OutputRoot 'bin\AnalogBoard_TestApp.exe'),
    (Join-Path $OutputRoot 'bin\AnalogBoard_Dll.dll')
)
$requiredEvidence = @(
    $deltaPath,
    $reviewPath,
    $unitTestLogPath,
    $dllBuildLogPath,
    $appBuildLogPath,
    $fieldIntegrityPath,
    $knownBaselineDeviationsPath
)
foreach ($path in @($artifactPaths + $requiredEvidence)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required build evidence is missing: $path"
    }
}

$sourceRelativePaths = @(
    'AnalogBoard_Dll\AnalogBoard_Dll.cpp',
    'AnalogBoard_Dll\AnalogBoard_Dll.h',
    'AnalogBoard_Dll\AnalogBoard_Dll.vcxproj',
    'AnalogBoard_Dll\Ep4FailureDiagnostic.h',
    'AnalogBoard_TestApp\AcquisitionShutdownCoordinator.h',
    'AnalogBoard_TestApp\AcquisitionCompletionLogic.h',
    'AnalogBoard_TestApp\ExternalTriggerPollingPolicy.h',
    'AnalogBoard_TestApp\RearmTelemetry.h',
    'AnalogBoard_TestApp\AnalogBoard_TestAppDlg.cpp',
    'AnalogBoard_TestApp\Dialog1_Main.cpp',
    'AnalogBoard_TestApp\Dialog1_Main.h',
    'AnalogBoard_TestApp\AnalogBoard_TestApp.vcxproj',
    'AnalogBoard_TestApp\AnalogBoard_TestApp.vcxproj.filters',
    'AnalogBoard_UnitTest\AcquisitionShutdownCoordinator_test.cpp',
    'AnalogBoard_UnitTest\ExternalTriggerPollingPolicy_test.cpp',
    'AnalogBoard_UnitTest\build_test.bat',
    'AnalogBoard_UnitTest\build_telemetry_graceful_stop_test.bat',
    'diagnostic_prep\telemetry_graceful_stop\DESIGN.md',
    'diagnostic_prep\telemetry_graceful_stop\FIELD_CHECKLIST.md',
    'diagnostic_prep\telemetry_graceful_stop\FIELD_TELEMETRY_CSV_PROCEDURE.md',
    'diagnostic_prep\telemetry_graceful_stop\FIELD_TELEMETRY_CSV_PROCEDURE.html',
    'diagnostic_prep\telemetry_graceful_stop\KNOWN_BASELINE_DEVIATIONS.txt',
    'diagnostic_prep\telemetry_graceful_stop\verify_source_overlay.ps1',
    'scripts\build_telemetry_graceful_stop.bat',
    'scripts\finalize_telemetry_graceful_stop_build.ps1'
)

$sourceFiles = foreach ($relativePath in $sourceRelativePaths) {
    $fullPath = Join-Path $SourceRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Source file is missing: $fullPath"
    }
    [ordered]@{
        path = $relativePath.Replace('\', '/')
        sha256 = Get-Sha256 $fullPath
    }
}

$overlayVerifier = Join-Path $prepRoot 'verify_source_overlay.ps1'
if (-not (Test-Path -LiteralPath $overlayVerifier -PathType Leaf)) {
    throw "Source overlay verifier is missing: $overlayVerifier"
}
& $overlayVerifier `
    -PackageRoot $PackageRoot `
    -SourceRoot $SourceRoot `
    -GracefulPatch $deltaPath `
    -OutputPath $overlayAttestationPath

$artifacts = foreach ($path in $artifactPaths) {
    $item = Get-Item -LiteralPath $path
    [ordered]@{
        path = Get-RelativePath $OutputRoot $path
        bytes = $item.Length
        sha256 = Get-Sha256 $path
    }
}

$manifest = [ordered]@{
    build_id = $BuildId
    created_at = (Get-Date).ToString('o')
    purpose = 'R7 validation build that defers application close until re-arm telemetry CSV, summary, and final log flush complete'
    supersedes_build_id = $package.known_build.build_id
    source = [ordered]@{
        package_id = $package.package_id
        base_tag = $package.host_source.base_tag
        head_commit = $package.host_source.head_commit
        original_tracked_patch_sha256 = $package.host_source.tracked_patch_sha256
        host_source_state = 'packaging baseline plus ep4_failure_diagnostic and telemetry_graceful_stop overlays; see source_overlay_attestation.json'
        overlay_attestation = 'manifest/source_overlay_attestation.json'
        overlay_attestation_sha256 = Get-Sha256 $overlayAttestationPath
        known_baseline_deviations = 'KNOWN_BASELINE_DEVIATIONS.txt'
        known_baseline_deviations_sha256 = Get-Sha256 $knownBaselineDeviationsPath
        source_delta = 'source_diff/graceful_stop_changes.patch'
        source_delta_sha256 = Get-Sha256 $deltaPath
        files = $sourceFiles
    }
    graceful_stop_contract = [ordered]@{
        operator_close_count = 1
        intended_session = 'low x3 plus high x30 without closing the application between runs'
        first_close = 'request EP6 thread cancellation and keep the window and logger alive'
        finalization_order = @('telemetry CSV write', 'REARM SUMMARY log', 'Exit EP6 get data thread log', 'log flush', 'asynchronous WM_CLOSE')
        telemetry_location = 'configured Save Path root; YYMMDD_HHMMSS_rearm_telemetry.csv with collision suffix when needed'
        close_during_active_measurement = 'not authorized; close only after the final PR01 CYCLE record and before another trigger'
        transfer_retry_changed = $false
        ep6_hot_path_changed = $false
        firmware_changed = $false
        driver_or_registry_changed = $false
    }
    tests = [ordered]@{
        tdd_red = 'AcquisitionShutdownCoordinator_test.cpp first failed because AcquisitionShutdownCoordinator.h did not exist'
        tdd_race_red = 'automatic-close-pending restart assertion failed before the finalized-close-pending state was added'
        portable = '36/36 coordinator assertions and 18/18 external-trigger polling assertions passed before native build'
        native_gate = 'coordinator, telemetry, telemetry replay, trigger polling, completion, EP4 diagnostic, EP6 retry, and wave-pair publish tests passed'
        native_gate_log = 'verification/unit_tests.txt'
    }
    review = [ordered]@{
        workflow = 'claude-review-fixer before Release artifact build'
        evidence = 'verification/claude_review.txt'
        sha256 = Get-Sha256 $reviewPath
    }
    build = [ordered]@{
        configuration = 'Release|x64'
        toolchain = 'Visual Studio 2022 v143 selected by scripts/run_with_vsdevcmd.bat'
        isolation = 'fresh diagnostic_builds/<build-id>/bin and obj directories; known build outputs are not overwritten'
        dll_log = 'verification/release_dll_build.txt'
        app_log = 'verification/release_app_build.txt'
        result = 'success'
    }
    artifacts = $artifacts
    required_runtime_files = @(
        [ordered]@{
            path = 'bin/default_config.csv'
            source = 'returned field_package/bin/default_config.csv runtime file copied into the new output'
            mutable_at_runtime = $true
        }
    )
    immutable_field_package_verification = 'verification/field_package_integrity.txt'
    hardware_status = 'not run; driver/DFX/USB/firmware changes and field acquisition require separate operator authorization'
    decision_boundary = 'R7 use for remaining legacy checks does not establish or change D4 baseline'
}

$utf8NoBom = New-Object Text.UTF8Encoding($false)
$json = $manifest | ConvertTo-Json -Depth 12
[IO.File]::WriteAllText($manifestPath, $json + [Environment]::NewLine, $utf8NoBom)

$buildChecksumRelativePaths = @(
    'BUILD_ID.txt',
    'DESIGN.md',
    'FIELD_CHECKLIST.md',
    'FIELD_TELEMETRY_CSV_PROCEDURE.md',
    'FIELD_TELEMETRY_CSV_PROCEDURE.html',
    'KNOWN_BASELINE_DEVIATIONS.txt',
    'bin\AnalogBoard_TestApp.exe',
    'bin\AnalogBoard_Dll.dll',
    'bin\default_config.csv',
    'manifest\build_manifest.json',
    'manifest\source_overlay_attestation.json',
    'source_diff\graceful_stop_changes.patch',
    'verification\claude_review.txt',
    'verification\field_package_integrity.txt',
    'verification\unit_tests.txt',
    'verification\release_dll_build.txt',
    'verification\release_app_build.txt'
)
Write-ChecksumManifest $OutputRoot $buildChecksumRelativePaths $checksumsPath $utf8NoBom

New-Item -ItemType Directory -Path $ReadyRoot | Out-Null
$readyEvidenceRoot = Join-Path $ReadyRoot 'evidence'
New-Item -ItemType Directory -Path $readyEvidenceRoot | Out-Null

$readyCopies = [ordered]@{
    (Join-Path $prepRoot '00_READ_ME_FIRST.txt') = (Join-Path $ReadyRoot '00_READ_ME_FIRST.txt')
    (Join-Path $prepRoot '01_START_APP.bat') = (Join-Path $ReadyRoot '01_START_APP.bat')
    (Join-Path $OutputRoot 'BUILD_ID.txt') = (Join-Path $ReadyRoot 'BUILD_ID.txt')
    (Join-Path $OutputRoot 'FIELD_CHECKLIST.md') = (Join-Path $ReadyRoot 'FIELD_CHECKLIST.md')
    (Join-Path $OutputRoot 'FIELD_TELEMETRY_CSV_PROCEDURE.md') = (Join-Path $ReadyRoot 'FIELD_TELEMETRY_CSV_PROCEDURE.md')
    (Join-Path $OutputRoot 'FIELD_TELEMETRY_CSV_PROCEDURE.html') = (Join-Path $ReadyRoot 'FIELD_TELEMETRY_CSV_PROCEDURE.html')
    $knownBaselineDeviationsPath = (Join-Path $ReadyRoot 'KNOWN_BASELINE_DEVIATIONS.txt')
    (Join-Path $OutputRoot 'bin\AnalogBoard_TestApp.exe') = (Join-Path $ReadyRoot 'AnalogBoard_TestApp.exe')
    (Join-Path $OutputRoot 'bin\AnalogBoard_Dll.dll') = (Join-Path $ReadyRoot 'AnalogBoard_Dll.dll')
    (Join-Path $OutputRoot 'bin\default_config.csv') = (Join-Path $ReadyRoot 'default_config.csv')
    $manifestPath = (Join-Path $readyEvidenceRoot 'build_manifest.json')
    $checksumsPath = (Join-Path $readyEvidenceRoot 'build_output_checksums.sha256')
    $overlayAttestationPath = (Join-Path $readyEvidenceRoot 'source_overlay_attestation.json')
    $deltaPath = (Join-Path $readyEvidenceRoot 'graceful_stop_changes.patch')
    $reviewPath = (Join-Path $readyEvidenceRoot 'claude_review.txt')
    $unitTestLogPath = (Join-Path $readyEvidenceRoot 'unit_tests.txt')
    $dllBuildLogPath = (Join-Path $readyEvidenceRoot 'release_dll_build.txt')
    $appBuildLogPath = (Join-Path $readyEvidenceRoot 'release_app_build.txt')
    $fieldIntegrityPath = (Join-Path $readyEvidenceRoot 'field_package_integrity.txt')
}
foreach ($entry in $readyCopies.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Key -PathType Leaf)) {
        throw "Ready-package source is missing: $($entry.Key)"
    }
    Copy-Item -LiteralPath $entry.Key -Destination $entry.Value
}

$readyChecksumRelativePaths = @(
    '00_READ_ME_FIRST.txt',
    '01_START_APP.bat',
    'BUILD_ID.txt',
    'FIELD_CHECKLIST.md',
    'FIELD_TELEMETRY_CSV_PROCEDURE.md',
    'FIELD_TELEMETRY_CSV_PROCEDURE.html',
    'KNOWN_BASELINE_DEVIATIONS.txt',
    'AnalogBoard_TestApp.exe',
    'AnalogBoard_Dll.dll',
    'default_config.csv',
    'evidence\build_manifest.json',
    'evidence\build_output_checksums.sha256',
    'evidence\source_overlay_attestation.json',
    'evidence\graceful_stop_changes.patch',
    'evidence\claude_review.txt',
    'evidence\unit_tests.txt',
    'evidence\release_dll_build.txt',
    'evidence\release_app_build.txt',
    'evidence\field_package_integrity.txt'
)
Write-ChecksumManifest $ReadyRoot $readyChecksumRelativePaths (Join-Path $readyEvidenceRoot 'checksums.sha256') $utf8NoBom

Write-Host "Build manifest: $manifestPath"
Write-Host "Build checksums: $checksumsPath"
Write-Host "Ready package: $ReadyRoot"
foreach ($artifact in $artifacts) {
    Write-Host ("{0}  {1}" -f $artifact.sha256, $artifact.path)
}
